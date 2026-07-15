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

#include "parser.h"

typedef struct {
    Token *toks;
    int    count;
    int    pos;
} Parser;

static Token *cur(Parser *p)  { return &p->toks[p->pos]; }
static Token *peek_at(Parser *p, int off) {
    int i = p->pos + off;
    if (i >= p->count) i = p->count - 1;
    return &p->toks[i];
}
static bool check(Parser *p, TokenType t) { return cur(p)->type == t; }
static bool at_end(Parser *p) { return cur(p)->type == TK_EOF; }

static Token *adv(Parser *p) {
    Token *t = cur(p);
    if (!at_end(p)) p->pos++;
    return t;
}

static void expect(Parser *p, TokenType t, const char *what) {
    if (!check(p, t)) {
        ich_fatal("line %d: parse error: expected %s but got '%s'",
                  cur(p)->line, what, token_type_name(cur(p)->type));
    }
    adv(p);
}

/* 改行スキップ */
static void skip_newlines(Parser *p) {
    while (check(p, TK_NEWLINE)) adv(p);
}

/* ---- 前方宣言 ---- */
static Node *parse_expr(Parser *p);
static Stmt *parse_stmt(Parser *p);
static void  parse_block_until(Parser *p, Block *b, TokenType stop1, TokenType stop2);

/* ---- 式 ---- */

/* 引数リスト: '(' の後から ')' まで。名前付き引数(answer=x)対応 */
static void parse_args(Parser *p, Node ***out_args, char ***out_names, int *out_c) {
    Node **args = NULL;
    char **names = NULL;
    int c = 0, cap = 0;
    expect(p, TK_LPAREN, "'('");
    skip_newlines(p);
    while (!check(p, TK_RPAREN)) {
        char *name = NULL;
        /* 名前付き引数: IDENT '=' expr */
        if (check(p, TK_IDENT) && peek_at(p, 1)->type == TK_ASSIGN) {
            name = ich_strdup(cur(p)->text);
            adv(p); /* ident */
            adv(p); /* = */
        }
        Node *e = parse_expr(p);
        if (c >= cap) {
            cap = cap ? cap * 2 : 4;
            args = ich_realloc(args, sizeof(Node *) * cap);
            names = ich_realloc(names, sizeof(char *) * cap);
        }
        args[c] = e;
        names[c] = name;
        c++;
        skip_newlines(p);
        if (check(p, TK_COMMA)) { adv(p); skip_newlines(p); continue; }
        break;
    }
    expect(p, TK_RPAREN, "')'");
    *out_args = args;
    *out_names = names;
    *out_c = c;
}

/* primary: number, string, ident, spc var, var ref, ( expr ) */
static Node *parse_primary(Parser *p) {
    Token *t = cur(p);
    int line = t->line;

    if (check(p, TK_NUMBER)) {
        adv(p);
        return node_number(t->num, line);
    }
    if (check(p, TK_STRING)) {
        adv(p);
        return node_string(t->text, line);
    }
    if (check(p, TK_LPAREN)) {
        adv(p);
        skip_newlines(p);
        Node *e = parse_expr(p);
        skip_newlines(p);
        expect(p, TK_RPAREN, "')'");
        return e;
    }
    /* spc var name */
    if (check(p, TK_SPC)) {
        adv(p);
        expect(p, TK_VAR, "'var' after 'spc'");
        if (!check(p, TK_IDENT)) {
            ich_fatal("line %d: parse error: expected special var name", line);
        }
        char *name = ich_strdup(cur(p)->text);
        adv(p);
        Node *n = node_spc_var(name, line);
        free(name);
        return n;
    }
    /* var x (グローバル値参照) */
    if (check(p, TK_VAR)) {
        adv(p);
        if (!check(p, TK_IDENT)) {
            ich_fatal("line %d: parse error: expected variable name after 'var'", line);
        }
        char *name = ich_strdup(cur(p)->text);
        adv(p);
        Node *n = node_var_ref(name, line);
        free(name);
        return n;
    }
    if (check(p, TK_IDENT)) {
        adv(p);
        return node_ident(t->text, line);
    }

    ich_fatal("line %d: parse error: unexpected token '%s' in expression",
              line, token_type_name(t->type));
    return NULL;
}

/* postfix: primary ( .member | [index] | (args) )* */
static Node *parse_postfix(Parser *p) {
    Node *e = parse_primary(p);
    for (;;) {
        int line = cur(p)->line;
        if (check(p, TK_DOT)) {
            adv(p);
            if (!check(p, TK_IDENT)) {
                ich_fatal("line %d: parse error: expected member name after '.'", line);
            }
            char *name = ich_strdup(cur(p)->text);
            adv(p);
            e = node_member(e, name, line);
            free(name);
        } else if (check(p, TK_LBRACKET)) {
            adv(p);
            Node *idx = parse_expr(p);
            expect(p, TK_RBRACKET, "']'");
            e = node_index(e, idx, line);
        } else if (check(p, TK_LPAREN)) {
            Node **args; char **names; int c;
            parse_args(p, &args, &names, &c);
            Node *call = ich_malloc(sizeof(Node));
            memset(call, 0, sizeof(Node));
            call->kind = NODE_CALL;
            call->line = line;
            call->as.call.callee = e;
            call->as.call.args = args;
            call->as.call.arg_names = names;
            call->as.call.argc = c;
            e = call;
        } else {
            break;
        }
    }
    return e;
}

/* unary: (not | '-') unary | postfix */
static Node *parse_unary(Parser *p) {
    if (check(p, TK_NOT) || check(p, TK_MINUS)) {
        int op = cur(p)->type;
        int line = cur(p)->line;
        adv(p);
        Node *operand = parse_unary(p);
        return node_unary(op, operand, line);
    }
    return parse_postfix(p);
}

/* mul: unary (('*'|'/') unary)* */
static Node *parse_mul(Parser *p) {
    Node *e = parse_unary(p);
    while (check(p, TK_STAR) || check(p, TK_SLASH)) {
        int op = cur(p)->type;
        int line = cur(p)->line;
        adv(p);
        Node *r = parse_unary(p);
        e = node_binary(op, e, r, line);
    }
    return e;
}

/* add: mul (('+'|'-') mul)* */
static Node *parse_add(Parser *p) {
    Node *e = parse_mul(p);
    while (check(p, TK_PLUS) || check(p, TK_MINUS)) {
        int op = cur(p)->type;
        int line = cur(p)->line;
        adv(p);
        Node *r = parse_mul(p);
        e = node_binary(op, e, r, line);
    }
    return e;
}

/* comparison: add ((gt|lt|eq|neq|ge|le) add)* */
static bool is_cmp(TokenType t) {
    return t == TK_GT || t == TK_LT || t == TK_EQ ||
           t == TK_NEQ || t == TK_GE || t == TK_LE;
}
static Node *parse_cmp(Parser *p) {
    Node *e = parse_add(p);
    while (is_cmp(cur(p)->type)) {
        int op = cur(p)->type;
        int line = cur(p)->line;
        adv(p);
        Node *r = parse_add(p);
        e = node_binary(op, e, r, line);
    }
    return e;
}

/* and: cmp (and cmp)* */
static Node *parse_and(Parser *p) {
    Node *e = parse_cmp(p);
    while (check(p, TK_AND)) {
        int line = cur(p)->line;
        adv(p);
        Node *r = parse_cmp(p);
        e = node_binary(TK_AND, e, r, line);
    }
    return e;
}

/* or: and (or and)* */
static Node *parse_or(Parser *p) {
    Node *e = parse_and(p);
    while (check(p, TK_OR)) {
        int line = cur(p)->line;
        adv(p);
        Node *r = parse_and(p);
        e = node_binary(TK_OR, e, r, line);
    }
    return e;
}

static Node *parse_expr(Parser *p) {
    return parse_or(p);
}

/* ---- 文 ---- */

/* exec: <block> end という形。'exec' ':' NEWLINE ブロック 'end' */
static void parse_exec_block(Parser *p, Block *b) {
    expect(p, TK_EXEC, "'exec'");
    expect(p, TK_COLON, "':'");
    skip_newlines(p);
    block_init(b);
    parse_block_until(p, b, TK_END, TK_EOF);
}

/* var / pvar 宣言 */
static Stmt *parse_var_decl(Parser *p, bool is_local) {
    int line = cur(p)->line;
    adv(p); /* var/pvar */
    if (!check(p, TK_IDENT)) {
        ich_fatal("line %d: parse error: expected variable name", line);
    }
    char *name = ich_strdup(cur(p)->text);
    adv(p);
    Node *init = NULL;
    if (check(p, TK_ASSIGN)) {
        adv(p);
        init = parse_expr(p);
    }
    Stmt *s = ich_malloc(sizeof(Stmt));
    memset(s, 0, sizeof(Stmt));
    s->kind = STMT_VAR_DECL;
    s->line = line;
    s->as.var_decl.is_local = is_local;
    s->as.var_decl.name = name;
    s->as.var_decl.init = init;
    return s;
}

/* 関数定義: IDENT '(' params ')' exec: ... end */
static Stmt *parse_func(Parser *p) {
    int line = cur(p)->line;
    char *name = ich_strdup(cur(p)->text);
    adv(p); /* name */
    expect(p, TK_LPAREN, "'('");
    char **params = NULL;
    int pc = 0, cap = 0;
    skip_newlines(p);
    while (!check(p, TK_RPAREN)) {
        if (!check(p, TK_IDENT)) {
            ich_fatal("line %d: parse error: expected parameter name", cur(p)->line);
        }
        if (pc >= cap) { cap = cap ? cap * 2 : 4; params = ich_realloc(params, sizeof(char *) * cap); }
        params[pc++] = ich_strdup(cur(p)->text);
        adv(p);
        if (check(p, TK_COMMA)) { adv(p); skip_newlines(p); continue; }
        break;
    }
    expect(p, TK_RPAREN, "')'");

    Stmt *s = ich_malloc(sizeof(Stmt));
    memset(s, 0, sizeof(Stmt));
    s->kind = STMT_FUNC;
    s->line = line;
    s->as.func.name = name;
    s->as.func.params = params;
    s->as.func.paramc = pc;
    parse_exec_block(p, &s->as.func.body);
    expect(p, TK_END, "'end'");
    return s;
}

/* if / while / pick などの制御構文と、代入/式文を判定 */
static Stmt *parse_stmt(Parser *p) {
    int line = cur(p)->line;

    /* spc var ich-ver=1 */
    if (check(p, TK_SPC)) {
        adv(p);
        expect(p, TK_VAR, "'var'");
        /* ich-ver=1 : IDENT '=' NUMBER */
        if (check(p, TK_IDENT) && strcmp(cur(p)->text, "ich-ver") == 0
            && peek_at(p, 1)->type == TK_ASSIGN) {
            adv(p); /* ich-ver */
            adv(p); /* = */
            if (!check(p, TK_NUMBER)) {
                ich_fatal("line %d: parse error: expected version number", line);
            }
            int ver = (int)cur(p)->num;
            adv(p);
            Stmt *s = ich_malloc(sizeof(Stmt));
            memset(s, 0, sizeof(Stmt));
            s->kind = STMT_SPC_VER;
            s->line = line;
            s->as.spc_ver.version = ver;
            return s;
        }
        ich_fatal("line %d: parse error: unsupported 'spc var' statement", line);
    }

    /* join */
    if (check(p, TK_JOIN)) {
        adv(p);
        char *module = NULL;
        bool is_path = false;
        if (check(p, TK_STRING)) {
            module = ich_strdup(cur(p)->text);
            is_path = true;
            adv(p);
        } else if (check(p, TK_LT)) {
            /* join <module>  : '<' IDENT '>' */
            adv(p);
            if (!check(p, TK_IDENT)) ich_fatal("line %d: parse error: expected module name", line);
            module = ich_strdup(cur(p)->text);
            adv(p);
            expect(p, TK_GT, "'>'");
        } else if (check(p, TK_IDENT)) {
            module = ich_strdup(cur(p)->text);
            adv(p);
        } else {
            ich_fatal("line %d: parse error: expected module after 'join'", line);
        }
        char *alias = NULL;
        if (check(p, TK_AS)) {
            adv(p);
            if (!check(p, TK_IDENT)) ich_fatal("line %d: parse error: expected alias name", line);
            alias = ich_strdup(cur(p)->text);
            adv(p);
        }
        Stmt *s = ich_malloc(sizeof(Stmt));
        memset(s, 0, sizeof(Stmt));
        s->kind = STMT_JOIN;
        s->line = line;
        s->as.join.module = module;
        s->as.join.alias = alias;
        s->as.join.is_path = is_path;
        return s;
    }

    /* var / pvar */
    if (check(p, TK_VAR))  return parse_var_decl(p, false);
    if (check(p, TK_PVAR)) return parse_var_decl(p, true);

    /* hold p = size */
    if (check(p, TK_HOLD)) {
        adv(p);
        if (!check(p, TK_IDENT)) ich_fatal("line %d: parse error: expected name after 'hold'", line);
        char *name = ich_strdup(cur(p)->text);
        adv(p);
        expect(p, TK_ASSIGN, "'='");
        Node *size = parse_expr(p);
        Stmt *s = ich_malloc(sizeof(Stmt));
        memset(s, 0, sizeof(Stmt));
        s->kind = STMT_HOLD;
        s->line = line;
        s->as.hold.name = name;
        s->as.hold.size = size;
        return s;
    }

    /* drop p */
    if (check(p, TK_DROP)) {
        adv(p);
        if (!check(p, TK_IDENT)) ich_fatal("line %d: parse error: expected name after 'drop'", line);
        char *name = ich_strdup(cur(p)->text);
        adv(p);
        Stmt *s = ich_malloc(sizeof(Stmt));
        memset(s, 0, sizeof(Stmt));
        s->kind = STMT_DROP;
        s->line = line;
        s->as.drop.name = name;
        return s;
    }

    /* if */
    if (check(p, TK_IF)) {
        adv(p);
        Node *cond = parse_expr(p);
        Stmt *s = ich_malloc(sizeof(Stmt));
        memset(s, 0, sizeof(Stmt));
        s->kind = STMT_IF;
        s->line = line;
        s->as.if_s.cond = cond;
        block_init(&s->as.if_s.else_blk);
        s->as.if_s.has_else = false;
        /* exec: ... (else exec: ...)? end */
        expect(p, TK_EXEC, "'exec'");
        expect(p, TK_COLON, "':'");
        skip_newlines(p);
        block_init(&s->as.if_s.then_blk);
        parse_block_until(p, &s->as.if_s.then_blk, TK_ELSE, TK_END);
        if (check(p, TK_ELSE)) {
            adv(p);
            s->as.if_s.has_else = true;
            expect(p, TK_EXEC, "'exec'");
            expect(p, TK_COLON, "':'");
            skip_newlines(p);
            parse_block_until(p, &s->as.if_s.else_blk, TK_END, TK_EOF);
        }
        expect(p, TK_END, "'end'");
        return s;
    }

    /* while */
    if (check(p, TK_WHILE)) {
        adv(p);
        Node *cond = parse_expr(p);
        Stmt *s = ich_malloc(sizeof(Stmt));
        memset(s, 0, sizeof(Stmt));
        s->kind = STMT_WHILE;
        s->line = line;
        s->as.while_s.cond = cond;
        parse_exec_block(p, &s->as.while_s.body);
        expect(p, TK_END, "'end'");
        return s;
    }

    /* ret */
    if (check(p, TK_RET)) {
        adv(p);
        Node *val = NULL;
        if (!check(p, TK_NEWLINE) && !at_end(p)) {
            val = parse_expr(p);
        }
        Stmt *s = ich_malloc(sizeof(Stmt));
        memset(s, 0, sizeof(Stmt));
        s->kind = STMT_RET;
        s->line = line;
        s->as.expr = val;
        return s;
    }

    /* pick: ... error: ... end */
    if (check(p, TK_PICK)) {
        adv(p);
        expect(p, TK_COLON, "':'");
        skip_newlines(p);
        Stmt *s = ich_malloc(sizeof(Stmt));
        memset(s, 0, sizeof(Stmt));
        s->kind = STMT_PICK;
        s->line = line;
        block_init(&s->as.pick_s.pick_blk);
        block_init(&s->as.pick_s.error_blk);
        parse_block_until(p, &s->as.pick_s.pick_blk, TK_ERROR, TK_END);
        if (check(p, TK_ERROR)) {
            adv(p);
            expect(p, TK_COLON, "':'");
            skip_newlines(p);
            parse_block_until(p, &s->as.pick_s.error_blk, TK_END, TK_EOF);
        }
        expect(p, TK_END, "'end'");
        return s;
    }

    /* 関数定義判定: IDENT '(' ... ')' exec :
       -- 単純に IDENT の後が '(' で、対応する ')' の後に 'exec' があれば関数 */
    if (check(p, TK_IDENT) && peek_at(p, 1)->type == TK_LPAREN) {
        /* 先読みして exec があるか探す */
        int save = p->pos;
        int depth = 0;
        int i = p->pos + 1; /* '(' */
        bool is_func = false;
        for (; i < p->count; i++) {
            TokenType tt = p->toks[i].type;
            if (tt == TK_LPAREN) depth++;
            else if (tt == TK_RPAREN) {
                depth--;
                if (depth == 0) {
                    if (i + 1 < p->count && p->toks[i + 1].type == TK_EXEC) {
                        is_func = true;
                    }
                    break;
                }
            } else if (tt == TK_EOF) break;
        }
        p->pos = save;
        if (is_func) {
            return parse_func(p);
        }
    }

    /* それ以外: 式 or 代入 */
    Node *lhs = parse_expr(p);
    if (check(p, TK_ASSIGN)) {
        adv(p);
        Node *rhs = parse_expr(p);
        Stmt *s = ich_malloc(sizeof(Stmt));
        memset(s, 0, sizeof(Stmt));
        s->kind = STMT_ASSIGN;
        s->line = line;
        s->as.assign.target = lhs;
        s->as.assign.value = rhs;
        return s;
    }
    Stmt *s = ich_malloc(sizeof(Stmt));
    memset(s, 0, sizeof(Stmt));
    s->kind = STMT_EXPR;
    s->line = line;
    s->as.expr = lhs;
    return s;
}

/* stop1 か stop2 が現れるまで文を読む(stop は消費しない) */
static void parse_block_until(Parser *p, Block *b, TokenType stop1, TokenType stop2) {
    skip_newlines(p);
    while (!check(p, stop1) && !check(p, stop2) && !at_end(p)) {
        Stmt *s = parse_stmt(p);
        block_push(b, s);
        /* 文の後は改行 or ブロック終端 */
        skip_newlines(p);
    }
}

Program parser_parse(TokenList *tl) {
    Parser p = { tl->tokens, tl->count, 0 };
    Program prog;
    prog.has_file_decl = false;
    prog.ich_ver = -1;
    block_init(&prog.stmts);

    skip_newlines(&p);

    /* ファイル宣言 (必須) */
    if (check(&p, TK_FILE_DECL)) {
        const char *d = cur(&p)->text;
        /* 大文字小文字無視で表記ゆれ許容 */
        if (d) {
            /* 許容: "Ichiyanagi Code", "ichiyanagi code", "iciyanagi code" */
            char low[64]; int j = 0;
            for (int i = 0; d[i] && j < 63; i++) {
                char c = d[i];
                if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
                low[j++] = c;
            }
            low[j] = '\0';
            if (strcmp(low, "ichiyanagi code") == 0 ||
                strcmp(low, "iciyanagi code") == 0) {
                prog.has_file_decl = true;
            }
        }
        adv(&p);
        skip_newlines(&p);
    }

    if (!prog.has_file_decl) {
        ich_fatal("compile error: missing file declaration '!\"Ichiyanagi Code\"' at top of file");
    }

    /* トップレベル文 */
    while (!at_end(&p)) {
        Stmt *s = parse_stmt(&p);
        block_push(&prog.stmts, s);
        if (s->kind == STMT_SPC_VER) prog.ich_ver = s->as.spc_ver.version;
        skip_newlines(&p);
    }

    return prog;
}
