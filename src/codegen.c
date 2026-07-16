/*
 * Copyright 2026 nyan<(nyan4)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Ichiyanagi V1 ネイティブコード生成バックエンド (SPEC 13 章)。
 * AST → x86-64 アセンブリ (Linux / System V AMD64 ABI / GAS AT&T 記法)。
 *
 * 値モデル: コンパイル対象サブセットでは全ての値を 64bit 符号付き整数として扱う。
 *           真偽値は 0/1。除算は整数除算。文字列は terminal.print の
 *           リテラル引数としてのみ扱える (.rodata に配置し write(2) で出力)。
 */

#include "codegen.h"
#include "lexer.h"
#include "common.h"
#include <stdarg.h>
#include <errno.h>

/* ---- 生成コンテキスト ---- */

typedef struct {
    char *name;
    int   offset;   /* rbp からのオフセット (負値) */
} CgLocal;

typedef struct {
    char *name;
    int   label_id; /* .Lstr<id> */
    char *data;     /* 文字列本体 */
} CgStr;

typedef struct {
    FILE   *out;
    Program *prog;

    /* 現在の関数のローカル変数テーブル */
    CgLocal *locals;
    int      local_count;
    int      local_cap;
    int      frame_size;   /* 割り当て済みスタックサイズ (16 境界) */
    int      next_offset;  /* 次に割り当てるローカルのオフセット */

    /* 文字列リテラルプール */
    CgStr   *strs;
    int      str_count;
    int      str_cap;

    /* グローバル変数名 (トップレベル var) */
    char   **globals;
    int      global_count;
    int      global_cap;

    int      label_seq;    /* 一意ラベル番号 */
    int      cur_ret_label; /* 現在の関数のエピローグラベル番号 */

    bool     failed;       /* 非対応/エラー検出フラグ */
    CodegenResult result;
    char     err[256];
} Cg;

/* ---- ユーティリティ ---- */

static void cg_fail(Cg *cg, CodegenResult r, const char *fmt, ...) {
    if (cg->failed) return;  /* 最初のエラーを保持 */
    cg->failed = true;
    cg->result = r;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cg->err, sizeof(cg->err), fmt, ap);
    va_end(ap);
}

static void emit(Cg *cg, const char *fmt, ...) {
    if (cg->failed) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(cg->out, fmt, ap);
    va_end(ap);
    fputc('\n', cg->out);
}

static int cg_new_label(Cg *cg) { return cg->label_seq++; }

/* ---- 文字列プール ---- */

static int intern_string(Cg *cg, const char *s) {
    for (int i = 0; i < cg->str_count; i++) {
        if (strcmp(cg->strs[i].data, s) == 0) return cg->strs[i].label_id;
    }
    if (cg->str_count >= cg->str_cap) {
        cg->str_cap = cg->str_cap ? cg->str_cap * 2 : 8;
        cg->strs = ich_realloc(cg->strs, sizeof(CgStr) * cg->str_cap);
    }
    int id = cg->str_count;
    cg->strs[id].label_id = id;
    cg->strs[id].data = ich_strdup(s);
    cg->strs[id].name = NULL;
    cg->str_count++;
    return id;
}

/* ---- ローカル/グローバル解決 ---- */

static CgLocal *find_local(Cg *cg, const char *name) {
    for (int i = cg->local_count - 1; i >= 0; i--) {
        if (strcmp(cg->locals[i].name, name) == 0) return &cg->locals[i];
    }
    return NULL;
}

static int is_global(Cg *cg, const char *name) {
    for (int i = 0; i < cg->global_count; i++) {
        if (strcmp(cg->globals[i], name) == 0) return 1;
    }
    return 0;
}

/* ローカル変数を割り当て (既存なら再利用) */
static CgLocal *alloc_local(Cg *cg, const char *name) {
    CgLocal *e = find_local(cg, name);
    if (e) return e;
    if (cg->local_count >= cg->local_cap) {
        cg->local_cap = cg->local_cap ? cg->local_cap * 2 : 8;
        cg->locals = ich_realloc(cg->locals, sizeof(CgLocal) * cg->local_cap);
    }
    cg->next_offset -= 8;
    cg->locals[cg->local_count].name = ich_strdup(name);
    cg->locals[cg->local_count].offset = cg->next_offset;
    return &cg->locals[cg->local_count++];
}

static void reset_locals(Cg *cg) {
    for (int i = 0; i < cg->local_count; i++) free(cg->locals[i].name);
    cg->local_count = 0;
    cg->next_offset = 0;
    cg->frame_size = 0;
}

/* ---- 前方宣言 ---- */
static void gen_expr(Cg *cg, Node *n);      /* 結果を %rax に */
static void gen_block(Cg *cg, Block *b);
static void gen_stmt(Cg *cg, Stmt *s);

/* 関数が存在するか (呼び出し検証用) */
static FuncDef *find_funcdef(Cg *cg, const char *name) {
    for (int i = 0; i < cg->prog->stmts.count; i++) {
        Stmt *s = cg->prog->stmts.items[i];
        if (s->kind == STMT_FUNC && strcmp(s->as.func.name, name) == 0)
            return &s->as.func;
    }
    return NULL;
}

/* ---- 式生成 ---- */

/* 変数のアドレス的取得: 値を %rax にロード */
static void gen_load_var(Cg *cg, const char *name) {
    CgLocal *l = find_local(cg, name);
    if (l) {
        emit(cg, "    movq %d(%%rbp), %%rax", l->offset);
        return;
    }
    if (is_global(cg, name)) {
        emit(cg, "    movq ich_g_%s(%%rip), %%rax", name);
        return;
    }
    cg_fail(cg, CODEGEN_UNSUPPORTED,
            "undefined variable '%s' (codegen)", name);
}

static void gen_binary(Cg *cg, Node *n) {
    int op = n->as.binary.op;

    /* 論理短絡 (and/or) */
    if (op == TK_AND || op == TK_OR) {
        int lend = cg_new_label(cg);
        gen_expr(cg, n->as.binary.l);
        emit(cg, "    cmpq $0, %%rax");
        /* 真偽正規化: rax を 0/1 に */
        if (op == TK_AND) {
            /* 左が偽なら 0 で確定 */
            emit(cg, "    je .Land_false_%d", lend);
            gen_expr(cg, n->as.binary.r);
            emit(cg, "    cmpq $0, %%rax");
            emit(cg, "    setne %%al");
            emit(cg, "    movzbq %%al, %%rax");
            emit(cg, "    jmp .Land_done_%d", lend);
            emit(cg, ".Land_false_%d:", lend);
            emit(cg, "    movq $0, %%rax");
            emit(cg, ".Land_done_%d:", lend);
        } else { /* OR */
            emit(cg, "    jne .Lor_true_%d", lend);
            gen_expr(cg, n->as.binary.r);
            emit(cg, "    cmpq $0, %%rax");
            emit(cg, "    setne %%al");
            emit(cg, "    movzbq %%al, %%rax");
            emit(cg, "    jmp .Lor_done_%d", lend);
            emit(cg, ".Lor_true_%d:", lend);
            emit(cg, "    movq $1, %%rax");
            emit(cg, ".Lor_done_%d:", lend);
        }
        return;
    }

    /* 通常二項: 左を評価しスタックへ、右を評価 */
    gen_expr(cg, n->as.binary.l);
    emit(cg, "    pushq %%rax");
    gen_expr(cg, n->as.binary.r);
    /* rax = 右, スタックトップ = 左 */
    emit(cg, "    movq %%rax, %%rcx");   /* rcx = 右 */
    emit(cg, "    popq %%rax");          /* rax = 左 */

    switch (op) {
        case TK_PLUS:  emit(cg, "    addq %%rcx, %%rax"); break;
        case TK_MINUS: emit(cg, "    subq %%rcx, %%rax"); break;
        case TK_STAR:  emit(cg, "    imulq %%rcx, %%rax"); break;
        case TK_SLASH:
            /* 整数除算: rax / rcx (符号付き) */
            emit(cg, "    cqto");           /* rdx:rax へ符号拡張 */
            emit(cg, "    idivq %%rcx");    /* 商 -> rax */
            break;
        case TK_GT: case TK_LT: case TK_GE:
        case TK_LE: case TK_EQ: case TK_NEQ: {
            const char *setcc = "sete";
            switch (op) {
                case TK_GT:  setcc = "setg"; break;
                case TK_LT:  setcc = "setl"; break;
                case TK_GE:  setcc = "setge"; break;
                case TK_LE:  setcc = "setle"; break;
                case TK_EQ:  setcc = "sete"; break;
                case TK_NEQ: setcc = "setne"; break;
            }
            emit(cg, "    cmpq %%rcx, %%rax");
            emit(cg, "    %s %%al", setcc);
            emit(cg, "    movzbq %%al, %%rax");
            break;
        }
        default:
            cg_fail(cg, CODEGEN_UNSUPPORTED, "unsupported binary operator (codegen)");
    }
}

/* terminal.print / printn の呼び出しを生成 */
static void gen_terminal_call(Cg *cg, Node *call, const char *method) {
    bool newline;
    if (strcmp(method, "print") == 0)       newline = true;
    else if (strcmp(method, "printn") == 0) newline = false;
    else {
        cg_fail(cg, CODEGEN_UNSUPPORTED,
                "terminal.%s is not supported by codegen (use interpreter)", method);
        return;
    }
    if (call->as.call.argc != 1) {
        cg_fail(cg, CODEGEN_UNSUPPORTED,
                "terminal.%s expects exactly 1 argument in codegen", method);
        return;
    }
    Node *arg = call->as.call.args[0];

    if (arg->kind == NODE_STRING) {
        /* 文字列リテラル: .rodata に置いて write */
        int id = intern_string(cg, arg->as.str);
        emit(cg, "    leaq .Lstr%d(%%rip), %%rdi", id);
        emit(cg, "    movq $%zu, %%rsi", strlen(arg->as.str));
        emit(cg, "    call ich_print_str");
        if (newline) emit(cg, "    call ich_print_nl");
    } else {
        /* 数値式: 評価して整数として出力 */
        gen_expr(cg, arg);
        emit(cg, "    movq %%rax, %%rdi");
        emit(cg, "    call ich_print_int");
        if (newline) emit(cg, "    call ich_print_nl");
    }
}

static void gen_call(Cg *cg, Node *n) {
    Node *callee = n->as.call.callee;

    if (callee->kind == NODE_MEMBER) {
        Node *objn = callee->as.member.obj;
        const char *method = callee->as.member.name;
        if (objn->kind == NODE_IDENT &&
            strcmp(objn->as.ident.name, "terminal") == 0) {
            gen_terminal_call(cg, n, method);
            return;
        }
        cg_fail(cg, CODEGEN_UNSUPPORTED,
                "module method '%s.%s' is not supported by codegen (use interpreter)",
                objn->kind == NODE_IDENT ? objn->as.ident.name : "<expr>", method);
        return;
    }

    if (callee->kind == NODE_IDENT) {
        const char *fname = callee->as.ident.name;
        if (strcmp(fname, "list") == 0 || strcmp(fname, "array") == 0) {
            cg_fail(cg, CODEGEN_UNSUPPORTED,
                    "'%s' (collections) not supported by codegen (use interpreter)", fname);
            return;
        }
        FuncDef *def = find_funcdef(cg, fname);
        if (!def) {
            cg_fail(cg, CODEGEN_UNSUPPORTED, "undefined function '%s' (codegen)", fname);
            return;
        }
        int argc = n->as.call.argc;
        if (argc != def->paramc) {
            cg_fail(cg, CODEGEN_ERROR,
                    "function '%s' expects %d args but got %d", fname, def->paramc, argc);
            return;
        }
        if (argc > 6) {
            cg_fail(cg, CODEGEN_UNSUPPORTED,
                    "functions with more than 6 args are not supported by codegen");
            return;
        }
        /* 引数を評価しスタックへ退避 (左→右)、その後レジスタへ */
        for (int i = 0; i < argc; i++) {
            gen_expr(cg, n->as.call.args[i]);
            emit(cg, "    pushq %%rax");
        }
        static const char *argregs[6] = {"%rdi","%rsi","%rdx","%rcx","%r8","%r9"};
        for (int i = argc - 1; i >= 0; i--) {
            emit(cg, "    popq %s", argregs[i]);
        }
        emit(cg, "    call ich_fn_%s", fname);
        return;
    }

    cg_fail(cg, CODEGEN_UNSUPPORTED, "unsupported call form (codegen)");
}

static void gen_expr(Cg *cg, Node *n) {
    if (cg->failed || !n) return;
    switch (n->kind) {
        case NODE_NUMBER: {
            /* V1 codegen は整数モデル。小数はエラー。 */
            double d = n->as.num;
            long long iv = (long long)d;
            if ((double)iv != d) {
                cg_fail(cg, CODEGEN_UNSUPPORTED,
                        "non-integer literal %g is not supported by codegen", d);
                return;
            }
            emit(cg, "    movq $%lld, %%rax", iv);
            break;
        }
        case NODE_STRING:
            cg_fail(cg, CODEGEN_UNSUPPORTED,
                    "string values are only supported as terminal.print arguments in codegen");
            break;
        case NODE_IDENT:
            gen_load_var(cg, n->as.ident.name);
            break;
        case NODE_VAR_REF: {
            const char *name = n->as.var_ref.name;
            if (!is_global(cg, name)) {
                cg_fail(cg, CODEGEN_UNSUPPORTED,
                        "'var %s' references unknown global (codegen)", name);
                return;
            }
            emit(cg, "    movq ich_g_%s(%%rip), %%rax", name);
            break;
        }
        case NODE_BINARY: gen_binary(cg, n); break;
        case NODE_UNARY: {
            gen_expr(cg, n->as.unary.operand);
            if (n->as.unary.op == TK_NOT) {
                emit(cg, "    cmpq $0, %%rax");
                emit(cg, "    sete %%al");
                emit(cg, "    movzbq %%al, %%rax");
            } else { /* 単項マイナス */
                emit(cg, "    negq %%rax");
            }
            break;
        }
        case NODE_CALL: gen_call(cg, n); break;
        case NODE_SPC_VAR:
            if (strcmp(n->as.spc_var.name, "ich-ver") == 0) {
                emit(cg, "    movq $%d, %%rax", ICH_VERSION);
            } else {
                cg_fail(cg, CODEGEN_UNSUPPORTED,
                        "spc var '%s' not supported by codegen (use interpreter)",
                        n->as.spc_var.name);
            }
            break;
        case NODE_INDEX:
            cg_fail(cg, CODEGEN_UNSUPPORTED,
                    "indexing (list/array) not supported by codegen (use interpreter)");
            break;
        case NODE_MEMBER:
            cg_fail(cg, CODEGEN_UNSUPPORTED,
                    "member access not supported by codegen (use interpreter)");
            break;
        default:
            cg_fail(cg, CODEGEN_UNSUPPORTED, "cannot compile expression (codegen)");
    }
}

/* ---- 代入 ---- */
static void gen_store_var(Cg *cg, const char *name, bool force_global, bool force_local) {
    /* 値は %rax にある想定 */
    if (force_global) {
        emit(cg, "    movq %%rax, ich_g_%s(%%rip)", name);
        return;
    }
    if (force_local) {
        CgLocal *l = alloc_local(cg, name);
        emit(cg, "    movq %%rax, %d(%%rbp)", l->offset);
        return;
    }
    /* 通常代入: ローカル優先、無ければグローバル、それも無ければローカル新規 */
    CgLocal *l = find_local(cg, name);
    if (l) { emit(cg, "    movq %%rax, %d(%%rbp)", l->offset); return; }
    if (is_global(cg, name)) {
        emit(cg, "    movq %%rax, ich_g_%s(%%rip)", name);
        return;
    }
    l = alloc_local(cg, name);
    emit(cg, "    movq %%rax, %d(%%rbp)", l->offset);
}

static void gen_stmt(Cg *cg, Stmt *s) {
    if (cg->failed) return;
    switch (s->kind) {
        case STMT_EXPR:
            gen_expr(cg, s->as.expr);
            break;
        case STMT_VAR_DECL: {
            if (s->as.var_decl.init) {
                gen_expr(cg, s->as.var_decl.init);
            } else {
                emit(cg, "    movq $0, %%rax");
            }
            gen_store_var(cg, s->as.var_decl.name,
                          !s->as.var_decl.is_local,   /* var -> global */
                          s->as.var_decl.is_local);   /* pvar -> local */
            break;
        }
        case STMT_ASSIGN: {
            Node *t = s->as.assign.target;
            gen_expr(cg, s->as.assign.value);
            if (t->kind == NODE_IDENT) {
                gen_store_var(cg, t->as.ident.name, false, false);
            } else if (t->kind == NODE_VAR_REF) {
                gen_store_var(cg, t->as.var_ref.name, true, false);
            } else {
                cg_fail(cg, CODEGEN_UNSUPPORTED,
                        "assignment target not supported by codegen (use interpreter)");
            }
            break;
        }
        case STMT_IF: {
            int lelse = cg_new_label(cg);
            int lend  = cg_new_label(cg);
            gen_expr(cg, s->as.if_s.cond);
            emit(cg, "    cmpq $0, %%rax");
            emit(cg, "    je .Lelse%d", lelse);
            gen_block(cg, &s->as.if_s.then_blk);
            emit(cg, "    jmp .Lendif%d", lend);
            emit(cg, ".Lelse%d:", lelse);
            if (s->as.if_s.has_else) gen_block(cg, &s->as.if_s.else_blk);
            emit(cg, ".Lendif%d:", lend);
            break;
        }
        case STMT_WHILE: {
            int ltop = cg_new_label(cg);
            int lend = cg_new_label(cg);
            emit(cg, ".Lwtop%d:", ltop);
            gen_expr(cg, s->as.while_s.cond);
            emit(cg, "    cmpq $0, %%rax");
            emit(cg, "    je .Lwend%d", lend);
            gen_block(cg, &s->as.while_s.body);
            emit(cg, "    jmp .Lwtop%d", ltop);
            emit(cg, ".Lwend%d:", lend);
            break;
        }
        case STMT_RET: {
            if (s->as.expr) gen_expr(cg, s->as.expr);
            else emit(cg, "    movq $0, %%rax");
            /* 関数エピローグ (共通ラベル) へジャンプ */
            emit(cg, "    jmp .Lepi%d", cg->cur_ret_label);
            break;
        }
        case STMT_HOLD:
        case STMT_DROP:
            cg_fail(cg, CODEGEN_UNSUPPORTED,
                    "hold/drop (heap) not supported by codegen (use interpreter)");
            break;
        case STMT_PICK:
            cg_fail(cg, CODEGEN_UNSUPPORTED,
                    "pick/error not supported by codegen (use interpreter)");
            break;
        case STMT_JOIN:
        case STMT_SPC_VER:
        case STMT_FUNC:
            /* トップレベルで処理済み or 無視 */
            break;
    }
}

static void gen_block(Cg *cg, Block *b) {
    for (int i = 0; i < b->count && !cg->failed; i++) {
        gen_stmt(cg, b->items[i]);
    }
}

/* ---- ローカル変数の事前スキャン (フレームサイズ決定) ---- */

/* ブロック内で宣言・代入される pvar/ローカル名を先に収集して割り当てる。
 * これにより if/while ネスト内の宣言もフレームに含められる。 */
static void prescan_target(Cg *cg, Node *t);

static void prescan_block(Cg *cg, Block *b) {
    for (int i = 0; i < b->count; i++) {
        Stmt *s = b->items[i];
        switch (s->kind) {
            case STMT_VAR_DECL:
                if (s->as.var_decl.is_local)
                    alloc_local(cg, s->as.var_decl.name);
                break;
            case STMT_ASSIGN:
                prescan_target(cg, s->as.assign.target);
                break;
            case STMT_IF:
                prescan_block(cg, &s->as.if_s.then_blk);
                if (s->as.if_s.has_else) prescan_block(cg, &s->as.if_s.else_blk);
                break;
            case STMT_WHILE:
                prescan_block(cg, &s->as.while_s.body);
                break;
            default: break;
        }
    }
}

static void prescan_target(Cg *cg, Node *t) {
    if (!t) return;
    if (t->kind == NODE_IDENT) {
        /* グローバルでなければローカルとして予約 */
        if (!is_global(cg, t->as.ident.name))
            alloc_local(cg, t->as.ident.name);
    }
    /* NODE_VAR_REF は常にグローバル。予約不要。 */
}

/* ---- 関数生成 ---- */

static void gen_func(Cg *cg, FuncDef *def) {
    reset_locals(cg);
    cg->cur_ret_label = cg_new_label(cg);

    static const char *argregs[6] = {"%rdi","%rsi","%rdx","%rcx","%r8","%r9"};

    /* 引数をローカルに割り当て */
    if (def->paramc > 6) {
        cg_fail(cg, CODEGEN_UNSUPPORTED,
                "function '%s' has more than 6 params (codegen)", def->name);
        return;
    }
    for (int i = 0; i < def->paramc; i++) alloc_local(cg, def->params[i]);

    /* 本体で使うローカルを事前スキャン */
    prescan_block(cg, &def->body);

    /* フレームサイズ (16 境界に丸め) */
    int frame = -cg->next_offset;
    if (frame % 16 != 0) frame += 16 - (frame % 16);
    cg->frame_size = frame;

    emit(cg, "");
    emit(cg, "    .globl ich_fn_%s", def->name);
    emit(cg, "    .type ich_fn_%s, @function", def->name);
    emit(cg, "ich_fn_%s:", def->name);
    emit(cg, "    pushq %%rbp");
    emit(cg, "    movq %%rsp, %%rbp");
    if (frame > 0) emit(cg, "    subq $%d, %%rsp", frame);

    /* 引数レジスタをローカルスロットへ格納 */
    for (int i = 0; i < def->paramc; i++) {
        CgLocal *l = find_local(cg, def->params[i]);
        emit(cg, "    movq %s, %d(%%rbp)", argregs[i], l->offset);
    }

    /* 本体 */
    gen_block(cg, &def->body);

    /* デフォルト戻り値 0 (ret が無い経路用) */
    emit(cg, "    movq $0, %%rax");
    emit(cg, ".Lepi%d:", cg->cur_ret_label);
    emit(cg, "    movq %%rbp, %%rsp");
    emit(cg, "    popq %%rbp");
    emit(cg, "    ret");
    emit(cg, "    .size ich_fn_%s, .-ich_fn_%s", def->name, def->name);
}

/* ---- ランタイムヘルパ (アセンブリで直接記述) ---- */

/*
 * self-contained な出力ヘルパを埋め込む:
 *   ich_print_int(long n)   : 10 進整数 (符号付き) を stdout へ
 *   ich_print_str(char*,len): 文字列を stdout へ (write syscall)
 *   ich_print_nl()          : 改行を出力
 * すべて System V AMD64 ABI に準拠。libc 非依存 (syscall 直叩き)。
 */
static void emit_runtime(Cg *cg) {
    fprintf(cg->out,
"\n"
"# ---- Ichiyanagi runtime helpers (libc-free, Linux x86-64) ----\n"
"    .text\n"
"# void ich_print_str(const char *buf in rdi, size_t len in rsi)\n"
"ich_print_str:\n"
"    movq %%rsi, %%rdx        # len\n"
"    movq %%rdi, %%rsi        # buf\n"
"    movq $1, %%rdi           # fd = stdout\n"
"    movq $1, %%rax           # syscall write\n"
"    syscall\n"
"    ret\n"
"\n"
"# void ich_print_nl(void)\n"
"ich_print_nl:\n"
"    subq $16, %%rsp\n"
"    movb $10, (%%rsp)        # '\\n'\n"
"    movq $1, %%rdi\n"
"    movq %%rsp, %%rsi\n"
"    movq $1, %%rdx\n"
"    movq $1, %%rax\n"
"    syscall\n"
"    addq $16, %%rsp\n"
"    ret\n"
"\n"
"# void ich_print_int(long n in rdi)\n"
"# 符号付き 10 進を一時バッファに逆順生成して write。\n"
"ich_print_int:\n"
"    pushq %%rbp\n"
"    movq %%rsp, %%rbp\n"
"    subq $64, %%rsp\n"
"    movq %%rdi, %%rax        # n\n"
"    leaq -8(%%rbp), %%rcx    # buf end (書き込みは後方から)\n"
"    movq $0, %%r8            # 負号フラグ\n"
"    cmpq $0, %%rax\n"
"    jne .Lpi_notzero\n"
"    # n == 0\n"
"    decq %%rcx\n"
"    movb $48, (%%rcx)        # '0'\n"
"    jmp .Lpi_emit\n"
".Lpi_notzero:\n"
"    jge .Lpi_loop\n"
"    movq $1, %%r8            # 負\n"
"    negq %%rax\n"
".Lpi_loop:\n"
"    cmpq $0, %%rax\n"
"    je .Lpi_sign\n"
"    movq $10, %%r9\n"
"    cqto\n"
"    idivq %%r9               # rax=商, rdx=余り\n"
"    addq $48, %%rdx          # '0' + digit\n"
"    decq %%rcx\n"
"    movb %%dl, (%%rcx)\n"
"    jmp .Lpi_loop\n"
".Lpi_sign:\n"
"    cmpq $0, %%r8\n"
"    je .Lpi_emit\n"
"    decq %%rcx\n"
"    movb $45, (%%rcx)        # '-'\n"
".Lpi_emit:\n"
"    # buf = rcx, len = (-8+rbp) - rcx\n"
"    leaq -8(%%rbp), %%rdx\n"
"    subq %%rcx, %%rdx        # len\n"
"    movq %%rcx, %%rsi        # buf\n"
"    movq $1, %%rdi\n"
"    movq $1, %%rax\n"
"    syscall\n"
"    movq %%rbp, %%rsp\n"
"    popq %%rbp\n"
"    ret\n"
    );
}

/* ---- .rodata (文字列プール) ---- */

static void emit_strings(Cg *cg) {
    if (cg->str_count == 0) return;
    fprintf(cg->out, "\n    .section .rodata\n");
    for (int i = 0; i < cg->str_count; i++) {
        fprintf(cg->out, ".Lstr%d:\n    .ascii \"", cg->strs[i].label_id);
        for (const char *p = cg->strs[i].data; *p; p++) {
            unsigned char c = (unsigned char)*p;
            switch (c) {
                case '\\': fputs("\\\\", cg->out); break;
                case '"':  fputs("\\\"", cg->out); break;
                case '\n': fputs("\\n", cg->out); break;
                case '\t': fputs("\\t", cg->out); break;
                case '\r': fputs("\\r", cg->out); break;
                default:
                    if (c < 0x20 || c >= 0x7f)
                        fprintf(cg->out, "\\%03o", c);
                    else
                        fputc(c, cg->out);
            }
        }
        fprintf(cg->out, "\"\n");
    }
}

/* ---- .bss (グローバル変数) ---- */

static void collect_globals(Cg *cg) {
    for (int i = 0; i < cg->prog->stmts.count; i++) {
        Stmt *s = cg->prog->stmts.items[i];
        if (s->kind == STMT_VAR_DECL && !s->as.var_decl.is_local) {
            const char *name = s->as.var_decl.name;
            if (is_global(cg, name)) continue;
            if (cg->global_count >= cg->global_cap) {
                cg->global_cap = cg->global_cap ? cg->global_cap * 2 : 8;
                cg->globals = ich_realloc(cg->globals, sizeof(char *) * cg->global_cap);
            }
            cg->globals[cg->global_count++] = ich_strdup(name);
        }
    }
}

static void emit_globals(Cg *cg) {
    if (cg->global_count == 0) return;
    fprintf(cg->out, "\n    .bss\n    .align 8\n");
    for (int i = 0; i < cg->global_count; i++) {
        fprintf(cg->out, "ich_g_%s:\n    .zero 8\n", cg->globals[i]);
    }
}

/* ---- エントリポイント生成 ---- */

/*
 * _start: トップレベル文 (var 宣言/代入など) を実行し main() を呼び、
 * その戻り値を終了コードに使って exit(2) する。
 */
static void gen_entry(Cg *cg) {
    emit(cg, "");
    emit(cg, "    .text");
    emit(cg, "    .globl _start");
    emit(cg, "_start:");
    emit(cg, "    # トップレベル (関数定義以外) を実行");
    /* _start をひとつの関数フレームとして扱う */
    reset_locals(cg);
    cg->cur_ret_label = cg_new_label(cg);

    /* トップレベルのローカル(=グローバル扱いだが念のため)スキャンは不要:
       トップレベルの var はグローバルに、代入はグローバル/ローカルへ。
       _start にローカルは基本発生しないが、pvar が書かれた場合に備え予約。 */
    Block top;
    block_init(&top);
    for (int i = 0; i < cg->prog->stmts.count; i++) {
        Stmt *s = cg->prog->stmts.items[i];
        if (s->kind == STMT_FUNC) continue;
        if (s->kind == STMT_SPC_VER || s->kind == STMT_JOIN) continue;
        block_push(&top, s);
    }
    prescan_block(cg, &top);
    int frame = -cg->next_offset;
    if (frame % 16 != 0) frame += 16 - (frame % 16);

    emit(cg, "    pushq %%rbp");
    emit(cg, "    movq %%rsp, %%rbp");
    if (frame > 0) emit(cg, "    subq $%d, %%rsp", frame);

    for (int i = 0; i < top.count && !cg->failed; i++) {
        gen_stmt(cg, top.items[i]);
    }
    free(top.items);

    /* main() を呼ぶ (存在すれば) */
    if (find_funcdef(cg, "main")) {
        emit(cg, "    call ich_fn_main");
    } else {
        emit(cg, "    movq $0, %%rax");
    }
    /* exit(rax) : rax は main の戻り値 */
    emit(cg, ".Lepi%d:", cg->cur_ret_label);
    emit(cg, "    movq %%rax, %%rdi   # exit code");
    emit(cg, "    movq $60, %%rax     # syscall exit");
    emit(cg, "    syscall");
}

/* ---- 公開: emit ---- */

CodegenResult codegen_emit(Program *prog, FILE *out) {
    Cg cg;
    memset(&cg, 0, sizeof(cg));
    cg.out = out;
    cg.prog = prog;
    cg.result = CODEGEN_OK;

    if (prog->ich_ver != -1 && prog->ich_ver != ICH_VERSION) {
        fprintf(stderr,
                "ichiyanagi: codegen: version mismatch (source ich-ver=%d, runtime=%d)\n",
                prog->ich_ver, ICH_VERSION);
        return CODEGEN_ERROR;
    }

    collect_globals(&cg);

    emit(&cg, "# Generated by Ichiyanagi V1 codegen (Tega-Neson)");
    emit(&cg, "# Target: x86-64 Linux (System V AMD64 ABI, ELF)");
    emit(&cg, "    .text");

    /* 各関数を生成 */
    for (int i = 0; i < prog->stmts.count && !cg.failed; i++) {
        Stmt *s = prog->stmts.items[i];
        if (s->kind == STMT_FUNC) gen_func(&cg, &s->as.func);
    }

    if (!cg.failed) gen_entry(&cg);
    if (!cg.failed) emit_runtime(&cg);
    if (!cg.failed) emit_strings(&cg);
    if (!cg.failed) emit_globals(&cg);

    CodegenResult r = cg.failed ? cg.result : CODEGEN_OK;
    if (cg.failed) {
        fprintf(stderr, "ichiyanagi: codegen: %s\n", cg.err);
    }

    /* 後片付け */
    for (int i = 0; i < cg.local_count; i++) free(cg.locals[i].name);
    free(cg.locals);
    for (int i = 0; i < cg.str_count; i++) free(cg.strs[i].data);
    free(cg.strs);
    for (int i = 0; i < cg.global_count; i++) free(cg.globals[i]);
    free(cg.globals);

    return r;
}

/* ---- 公開: compile to file (as/ld 駆動) ---- */

int codegen_compile_to_file(Program *prog, const char *out_path, bool emit_asm_only) {
    char asm_path[1024];
    char obj_path[1024];

    if (emit_asm_only) {
        snprintf(asm_path, sizeof(asm_path), "%s", out_path);
    } else {
        snprintf(asm_path, sizeof(asm_path), "%s.s", out_path);
    }
    snprintf(obj_path, sizeof(obj_path), "%s.o", out_path);

    FILE *f = fopen(asm_path, "w");
    if (!f) {
        fprintf(stderr, "ichiyanagi: cannot write asm to '%s': %s\n",
                asm_path, strerror(errno));
        return 1;
    }
    CodegenResult r = codegen_emit(prog, f);
    fclose(f);
    if (r != CODEGEN_OK) {
        if (!emit_asm_only) remove(asm_path);
        return (int)r;
    }

    if (emit_asm_only) {
        fprintf(stderr, "ichiyanagi: wrote assembly to %s\n", asm_path);
        return 0;
    }

    /* as でアセンブル */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "as --64 -o \"%s\" \"%s\"", obj_path, asm_path);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "ichiyanagi: assembler (as) failed (rc=%d)\n", rc);
        return 1;
    }

    /* ld でリンク (libc 非依存 / _start エントリ) */
    snprintf(cmd, sizeof(cmd), "ld -o \"%s\" \"%s\"", out_path, obj_path);
    rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "ichiyanagi: linker (ld) failed (rc=%d)\n", rc);
        return 1;
    }

    /* 中間物を掃除 */
    remove(asm_path);
    remove(obj_path);

    fprintf(stderr, "ichiyanagi: compiled native executable -> %s\n", out_path);
    return 0;
}
