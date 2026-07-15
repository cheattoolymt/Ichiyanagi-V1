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

#include "interp.h"
#include "module.h"
#include "lexer.h"
#include <math.h>

/* ---- スコープ操作 ---- */

static void scope_init(Scope *s, Scope *parent) {
    s->vars = NULL; s->count = 0; s->cap = 0; s->parent = parent;
}

static void scope_free(Scope *s) {
    for (int i = 0; i < s->count; i++) {
        free(s->vars[i].name);
        value_release(s->vars[i].value);
    }
    free(s->vars);
    s->vars = NULL; s->count = s->cap = 0;
}

/* スコープ内でのみ検索 */
static VarEntry *scope_find_local(Scope *s, const char *name) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->vars[i].name, name) == 0) return &s->vars[i];
    }
    return NULL;
}

void scope_declare(Scope *s, const char *name, Value v) {
    VarEntry *e = scope_find_local(s, name);
    if (e) {
        value_release(e->value);
        e->value = value_retain(v);
        return;
    }
    if (s->count >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->vars = ich_realloc(s->vars, sizeof(VarEntry) * s->cap);
    }
    s->vars[s->count].name = ich_strdup(name);
    s->vars[s->count].value = value_retain(v);
    s->count++;
}

/* 現在スコープ→...→グローバル の順で検索して取得 */
Value scope_get(Interp *it, const char *name, bool *found) {
    /* ローカル(current) を優先 */
    if (it->current && it->current != &it->globals) {
        VarEntry *e = scope_find_local(it->current, name);
        if (e) { if (found) *found = true; return value_retain(e->value); }
    }
    VarEntry *g = scope_find_local(&it->globals, name);
    if (g) { if (found) *found = true; return value_retain(g->value); }
    if (found) *found = false;
    return value_nil();
}

/* 代入: 既存のローカル→グローバルを探し、無ければ現在スコープに新規作成 */
void scope_set_existing_or_global(Interp *it, const char *name, Value v) {
    if (it->current && it->current != &it->globals) {
        VarEntry *e = scope_find_local(it->current, name);
        if (e) { value_release(e->value); e->value = value_retain(v); return; }
    }
    VarEntry *g = scope_find_local(&it->globals, name);
    if (g) { value_release(g->value); g->value = value_retain(v); return; }
    /* 未宣言 -> 現在スコープに宣言 */
    scope_declare(it->current, name, v);
}

/* ---- 関数テーブル ---- */

static void register_func(Interp *it, FuncDef *def) {
    if (it->func_count >= it->func_cap) {
        it->func_cap = it->func_cap ? it->func_cap * 2 : 8;
        it->funcs = ich_realloc(it->funcs, sizeof(FuncEntry) * it->func_cap);
    }
    it->funcs[it->func_count].name = ich_strdup(def->name);
    it->funcs[it->func_count].def = def;
    it->func_count++;
}

static FuncDef *find_func(Interp *it, const char *name) {
    for (int i = 0; i < it->func_count; i++) {
        if (strcmp(it->funcs[i].name, name) == 0) return it->funcs[i].def;
    }
    return NULL;
}

/* ---- 演算 ---- */

static double as_num(Value v) {
    switch (v.type) {
        case VAL_NUMBER: return v.as.num;
        case VAL_BOOL:   return v.as.boolean ? 1 : 0;
        default:
            err_raise(ERR_TYPE, "expected number but got %s", value_type_name(v));
            return 0;
    }
}

static Value eval_binary(Interp *it, Node *n) {
    int op = n->as.binary.op;

    /* 論理演算は短絡評価 */
    if (op == TK_AND) {
        Value l = interp_eval(it, n->as.binary.l);
        if (!value_truthy(l)) { value_release(l); return value_bool(false); }
        value_release(l);
        Value r = interp_eval(it, n->as.binary.r);
        bool res = value_truthy(r);
        value_release(r);
        return value_bool(res);
    }
    if (op == TK_OR) {
        Value l = interp_eval(it, n->as.binary.l);
        if (value_truthy(l)) { value_release(l); return value_bool(true); }
        value_release(l);
        Value r = interp_eval(it, n->as.binary.r);
        bool res = value_truthy(r);
        value_release(r);
        return value_bool(res);
    }

    Value l = interp_eval(it, n->as.binary.l);
    Value r = interp_eval(it, n->as.binary.r);
    Value result = value_nil();

    switch (op) {
        case TK_PLUS:
            /* 文字列連結 or 数値加算 */
            if (l.type == VAL_STRING || r.type == VAL_STRING) {
                char *ls = value_to_cstr(l);
                char *rs = value_to_cstr(r);
                size_t n1 = strlen(ls), n2 = strlen(rs);
                char *buf = ich_malloc(n1 + n2 + 1);
                memcpy(buf, ls, n1);
                memcpy(buf + n1, rs, n2 + 1);
                result = value_string(buf);
                free(ls); free(rs); free(buf);
            } else {
                result = value_number(as_num(l) + as_num(r));
            }
            break;
        case TK_MINUS: result = value_number(as_num(l) - as_num(r)); break;
        case TK_STAR:  result = value_number(as_num(l) * as_num(r)); break;
        case TK_SLASH: {
            double d = as_num(r);
            if (d == 0.0) {
                value_release(l); value_release(r);
                err_raise(ERR_DIV_ZERO, "division by zero");
            }
            result = value_number(as_num(l) / d);
            break;
        }
        case TK_GT:  result = value_bool(as_num(l) >  as_num(r)); break;
        case TK_LT:  result = value_bool(as_num(l) <  as_num(r)); break;
        case TK_GE:  result = value_bool(as_num(l) >= as_num(r)); break;
        case TK_LE:  result = value_bool(as_num(l) <= as_num(r)); break;
        case TK_EQ:  result = value_bool(value_equals(l, r)); break;
        case TK_NEQ: result = value_bool(!value_equals(l, r)); break;
        default:
            err_raise(ERR_RUNTIME, "unknown binary operator");
    }
    value_release(l);
    value_release(r);
    return result;
}

/* callee が モジュールメソッド(terminal.print等) か通常呼び出しかを評価 */
static Value eval_call(Interp *it, Node *n) {
    Node *callee = n->as.call.callee;

    /* args を評価 */
    int argc = n->as.call.argc;
    Value *args = argc ? ich_malloc(sizeof(Value) * argc) : NULL;
    for (int i = 0; i < argc; i++) {
        args[i] = interp_eval(it, n->as.call.args[i]);
    }
    char **argnames = n->as.call.arg_names;

    Value result = value_nil();

    if (callee->kind == NODE_MEMBER) {
        /* obj.method(...) : obj がモジュールか値か */
        Node *objn = callee->as.member.obj;
        const char *method = callee->as.member.name;

        /* obj が識別子でモジュール名の場合 */
        if (objn->kind == NODE_IDENT) {
            ModuleId mod = module_lookup(objn->as.ident.name);
            /* 変数として定義されていないか確認(モジュール優先だが変数があればそちら) */
            bool found = false;
            Value ov = scope_get(it, objn->as.ident.name, &found);
            if (!found && mod != MOD_UNKNOWN) {
                value_release(ov);
                result = module_call(it, mod, method, args, argnames, argc, n);
                goto done;
            }
            if (found && ov.type == VAL_MODULE) {
                ModuleId m2 = (ModuleId)ov.as.module_id;
                value_release(ov);
                result = module_call(it, m2, method, args, argnames, argc, n);
                goto done;
            }
            /* 値のメソッド (list/array) */
            if (found) {
                bool handled = false;
                Value r = value_method_call(it, ov, method, args, argc, &handled);
                value_release(ov);
                if (handled) { result = r; goto done; }
                err_raise(ERR_NAME, "no such method '%s'", method);
            }
            value_release(ov);
            /* モジュールでもなければエラー */
            err_raise(ERR_NAME, "undefined module or variable '%s'", objn->as.ident.name);
        } else {
            /* 式の結果に対するメソッド */
            Value ov = interp_eval(it, objn);
            if (ov.type == VAL_MODULE) {
                ModuleId m2 = (ModuleId)ov.as.module_id;
                value_release(ov);
                result = module_call(it, m2, method, args, argnames, argc, n);
                goto done;
            }
            bool handled = false;
            Value r = value_method_call(it, ov, method, args, argc, &handled);
            value_release(ov);
            if (handled) { result = r; goto done; }
            err_raise(ERR_NAME, "no such method '%s'", method);
        }
    } else if (callee->kind == NODE_IDENT) {
        const char *fname = callee->as.ident.name;
        /* 組み込みグローバル関数 (list/array) */
        bool handled = false;
        Value r = builtin_call(it, fname, args, argc, &handled);
        if (handled) { result = r; goto done; }

        /* ユーザー定義関数 */
        FuncDef *def = find_func(it, fname);
        if (!def) {
            err_raise(ERR_NAME, "undefined function '%s'", fname);
        }
        if (argc != def->paramc) {
            err_raise(ERR_ARGUMENT, "function '%s' expects %d args but got %d",
                      fname, def->paramc, argc);
        }
        /* 新しいローカルスコープ */
        Scope local;
        scope_init(&local, &it->globals);
        for (int i = 0; i < def->paramc; i++) {
            scope_declare(&local, def->params[i], args[i]);
        }
        Scope *saved = it->current;
        bool saved_ret = it->returning;
        Value saved_rv = it->return_value;
        it->current = &local;
        it->returning = false;
        it->return_value = value_nil();

        interp_exec_block(it, &def->body);

        Value rv = it->returning ? it->return_value : value_nil();
        it->current = saved;
        it->returning = saved_ret;
        it->return_value = saved_rv;
        scope_free(&local);
        result = rv;
        goto done;
    } else {
        err_raise(ERR_RUNTIME, "value is not callable");
    }

done:
    for (int i = 0; i < argc; i++) value_release(args[i]);
    free(args);
    return result;
}

Value interp_eval(Interp *it, Node *n) {
    if (!n) return value_nil();
    switch (n->kind) {
        case NODE_NUMBER: return value_number(n->as.num);
        case NODE_STRING: return value_string(n->as.str);
        case NODE_IDENT: {
            /* モジュール名参照(単独) */
            ModuleId mod = module_lookup(n->as.ident.name);
            bool found = false;
            Value v = scope_get(it, n->as.ident.name, &found);
            if (found) return v;
            value_release(v);
            if (mod != MOD_UNKNOWN) return value_module(mod);
            err_raise(ERR_NAME, "undefined variable '%s'", n->as.ident.name);
        }
        case NODE_VAR_REF: {
            /* var x : グローバル値を取得 */
            VarEntry *g = NULL;
            for (int i = 0; i < it->globals.count; i++) {
                if (strcmp(it->globals.vars[i].name, n->as.var_ref.name) == 0) {
                    g = &it->globals.vars[i]; break;
                }
            }
            if (!g) err_raise(ERR_NAME, "undefined global variable '%s'", n->as.var_ref.name);
            return value_retain(g->value);
        }
        case NODE_BINARY: return eval_binary(it, n);
        case NODE_UNARY: {
            Value v = interp_eval(it, n->as.unary.operand);
            Value r;
            if (n->as.unary.op == TK_NOT) {
                r = value_bool(!value_truthy(v));
            } else { /* MINUS */
                r = value_number(-as_num(v));
            }
            value_release(v);
            return r;
        }
        case NODE_CALL: return eval_call(it, n);
        case NODE_INDEX: {
            Value obj = interp_eval(it, n->as.index.obj);
            Value idx = interp_eval(it, n->as.index.index);
            if (obj.type != VAL_LIST && obj.type != VAL_ARRAY) {
                value_release(obj); value_release(idx);
                err_raise(ERR_TYPE, "index target is not a list/array");
            }
            int i = (int)as_num(idx);
            if (i < 0 || i >= obj.as.arr->count) {
                value_release(obj); value_release(idx);
                err_raise(ERR_INDEX, "index %d out of range", i);
            }
            Value r = arr_get(obj.as.arr, i);
            value_release(obj); value_release(idx);
            return r;
        }
        case NODE_SPC_VAR: {
            /* err-msg / err-code */
            if (strcmp(n->as.spc_var.name, "err-msg") == 0) {
                return value_string(err_current_message());
            }
            if (strcmp(n->as.spc_var.name, "err-code") == 0) {
                return value_number((double)err_current_code());
            }
            if (strcmp(n->as.spc_var.name, "ich-ver") == 0) {
                return value_number(ICH_VERSION);
            }
            err_raise(ERR_NAME, "unknown special var '%s'", n->as.spc_var.name);
        }
        case NODE_MEMBER: {
            /* 単独 obj.member (モジュール参照など) - 呼び出しなし */
            if (n->as.member.obj->kind == NODE_IDENT) {
                ModuleId mod = module_lookup(n->as.member.obj->as.ident.name);
                if (mod != MOD_UNKNOWN) return value_module(mod);
            }
            err_raise(ERR_NAME, "cannot access member without call");
        }
        default:
            err_raise(ERR_RUNTIME, "cannot evaluate node");
    }
    return value_nil();
}

/* 代入対象への書き込み */
static void do_assign(Interp *it, Node *target, Value v) {
    if (target->kind == NODE_IDENT) {
        scope_set_existing_or_global(it, target->as.ident.name, v);
    } else if (target->kind == NODE_VAR_REF) {
        /* var x = ... : グローバルへ */
        scope_declare(&it->globals, target->as.var_ref.name, v);
    } else if (target->kind == NODE_INDEX) {
        Value obj = interp_eval(it, target->as.index.obj);
        Value idx = interp_eval(it, target->as.index.index);
        if (obj.type != VAL_LIST && obj.type != VAL_ARRAY) {
            value_release(obj); value_release(idx);
            err_raise(ERR_TYPE, "index assign target is not a list/array");
        }
        int i = (int)as_num(idx);
        if (obj.type == VAL_ARRAY && (i < 0 || i >= obj.as.arr->count)) {
            value_release(obj); value_release(idx);
            err_raise(ERR_INDEX, "array index %d out of range", i);
        }
        arr_set(obj.as.arr, i, v);
        value_release(obj); value_release(idx);
    } else {
        err_raise(ERR_RUNTIME, "invalid assignment target");
    }
}

static void exec_stmt(Interp *it, Stmt *s) {
    if (it->returning) return;
    switch (s->kind) {
        case STMT_EXPR: {
            Value v = interp_eval(it, s->as.expr);
            value_release(v);
            break;
        }
        case STMT_VAR_DECL: {
            Value v = s->as.var_decl.init ? interp_eval(it, s->as.var_decl.init)
                                          : value_nil();
            if (s->as.var_decl.is_local) {
                scope_declare(it->current, s->as.var_decl.name, v);
            } else {
                scope_declare(&it->globals, s->as.var_decl.name, v);
            }
            value_release(v);
            break;
        }
        case STMT_HOLD: {
            Value sz = interp_eval(it, s->as.hold.size);
            size_t bytes = (size_t)as_num(sz);
            value_release(sz);
            Value h = value_heap(bytes);
            scope_declare(it->current, s->as.hold.name, h);
            value_release(h);
            break;
        }
        case STMT_DROP: {
            /* 変数から heap を取り除き解放 (nilに置き換え) */
            Value nilv = value_nil();
            scope_set_existing_or_global(it, s->as.drop.name, nilv);
            break;
        }
        case STMT_ASSIGN: {
            Value v = interp_eval(it, s->as.assign.value);
            do_assign(it, s->as.assign.target, v);
            value_release(v);
            break;
        }
        case STMT_IF: {
            Value c = interp_eval(it, s->as.if_s.cond);
            bool t = value_truthy(c);
            value_release(c);
            if (t) interp_exec_block(it, &s->as.if_s.then_blk);
            else if (s->as.if_s.has_else) interp_exec_block(it, &s->as.if_s.else_blk);
            break;
        }
        case STMT_WHILE: {
            for (;;) {
                Value c = interp_eval(it, s->as.while_s.cond);
                bool t = value_truthy(c);
                value_release(c);
                if (!t) break;
                interp_exec_block(it, &s->as.while_s.body);
                if (it->returning) break;
            }
            break;
        }
        case STMT_RET: {
            Value v = s->as.expr ? interp_eval(it, s->as.expr) : value_nil();
            it->return_value = v;  /* 所有権を移す */
            it->returning = true;
            break;
        }
        case STMT_PICK: {
            ErrorFrame frame;
            if (setjmp(frame.env) == 0) {
                err_push_frame(&frame);
                interp_exec_block(it, &s->as.pick_s.pick_blk);
                err_pop_frame();
            } else {
                /* エラー発生 -> error ブロックへ。frame は raise 側で pop 済み */
                interp_exec_block(it, &s->as.pick_s.error_blk);
                err_clear_current();
            }
            break;
        }
        case STMT_JOIN:
            /* V1: 標準モジュールは常に利用可能。join はエイリアス登録のみ扱う。
               標準モジュール名でエイリアスがあれば変数として登録 */
            if (s->as.join.alias && !s->as.join.is_path) {
                ModuleId mod = module_lookup(s->as.join.module);
                if (mod != MOD_UNKNOWN) {
                    Value mv = value_module(mod);
                    scope_declare(&it->globals, s->as.join.alias, mv);
                    value_release(mv);
                }
            }
            break;
        case STMT_SPC_VER:
            /* パース済み。ここでは何もしない */
            break;
        case STMT_FUNC:
            /* 実行前に登録済み。ここでは何もしない */
            break;
    }
}

void interp_exec_block(Interp *it, Block *b) {
    for (int i = 0; i < b->count; i++) {
        if (it->returning) break;
        exec_stmt(it, b->items[i]);
    }
}

int interp_run(Program *prog, int argc, char **argv) {
    /* バージョンチェック */
    if (prog->ich_ver != -1 && prog->ich_ver != ICH_VERSION) {
        fprintf(stderr,
                "ichiyanagi: version mismatch: source requires ich-ver=%d "
                "but this runtime is ich-ver=%d\n",
                prog->ich_ver, ICH_VERSION);
        return 1;
    }

    Interp it;
    memset(&it, 0, sizeof(it));
    scope_init(&it.globals, NULL);
    it.current = &it.globals;
    it.argc = argc;
    it.argv = argv;
    it.returning = false;
    it.return_value = value_nil();

    /* 1) 関数定義を先に登録 */
    for (int i = 0; i < prog->stmts.count; i++) {
        Stmt *s = prog->stmts.items[i];
        if (s->kind == STMT_FUNC) register_func(&it, &s->as.func);
    }

    /* 2) トップレベル文を順に実行(関数定義以外)。
          var 宣言 / join / spc var 等を処理 */
    for (int i = 0; i < prog->stmts.count; i++) {
        Stmt *s = prog->stmts.items[i];
        if (s->kind == STMT_FUNC) continue;
        exec_stmt(&it, s);
    }

    int exit_code = 0;
    /* 3) main() があれば呼び出す */
    FuncDef *mainf = find_func(&it, "main");
    if (mainf) {
        Scope local;
        scope_init(&local, &it.globals);
        it.current = &local;
        it.returning = false;
        Value savedrv = it.return_value;
        it.return_value = value_nil();
        interp_exec_block(&it, &mainf->body);
        if (it.returning && it.return_value.type == VAL_NUMBER) {
            exit_code = (int)it.return_value.as.num;
        }
        value_release(it.return_value);
        it.return_value = savedrv;
        it.current = &it.globals;
        scope_free(&local);
    }

    /* 後片付け */
    value_release(it.return_value);
    scope_free(&it.globals);
    for (int i = 0; i < it.func_count; i++) free(it.funcs[i].name);
    free(it.funcs);
    err_clear_current();

    return exit_code;
}
