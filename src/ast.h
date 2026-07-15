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

#ifndef ICHIYANAGI_AST_H
#define ICHIYANAGI_AST_H

#include "common.h"

/* ---- 式ノード ---- */
typedef enum {
    NODE_NUMBER,
    NODE_STRING,
    NODE_IDENT,       /* 変数参照 */
    NODE_VAR_REF,     /* var x  (グローバル値の明示参照) */
    NODE_BINARY,      /* 二項演算 */
    NODE_UNARY,       /* 単項 (not / 負号) */
    NODE_CALL,        /* 関数/メソッド呼び出し: callee(args) */
    NODE_MEMBER,      /* obj.member (モジュール/メソッド用) */
    NODE_INDEX,       /* arr[expr] */
    NODE_SPC_VAR,     /* spc var name (err-msg 等) */
    NODE_LIST_LIT     /* list(...) は CALL で扱うが、内部生成に利用可 */
} NodeKind;

/* ---- 文ノード ---- */
typedef enum {
    STMT_EXPR,        /* 式文 */
    STMT_VAR_DECL,    /* var / pvar 宣言 */
    STMT_HOLD,        /* hold p = size */
    STMT_DROP,        /* drop p */
    STMT_ASSIGN,      /* target = expr  (target は IDENT or INDEX or MEMBER) */
    STMT_IF,
    STMT_WHILE,
    STMT_RET,
    STMT_PICK,        /* pick / error */
    STMT_JOIN,        /* join module */
    STMT_SPC_VER,     /* spc var ich-ver=1 */
    STMT_FUNC         /* 関数定義 */
} StmtKind;

typedef struct Node Node;
typedef struct Stmt Stmt;

/* 文リスト(ブロック) */
typedef struct {
    Stmt **items;
    int    count;
    int    cap;
} Block;

/* 式ノード */
struct Node {
    NodeKind kind;
    int      line;
    union {
        double num;                       /* NUMBER */
        char  *str;                       /* STRING / IDENT / SPC_VAR name */
        struct { char *name; } ident;     /* IDENT */
        struct { char *name; } var_ref;   /* VAR_REF */
        struct { int op; Node *l, *r; } binary; /* op は TokenType */
        struct { int op; Node *operand; } unary;
        struct {
            Node  *callee;   /* 呼ばれる対象 (IDENT or MEMBER) */
            Node **args;
            int    argc;
            /* 名前付き引数(answer=x 用) */
            char **arg_names; /* 各 args に対応。無名は NULL */
        } call;
        struct { Node *obj; char *name; } member;
        struct { Node *obj; Node *index; } index;
        struct { char *name; } spc_var;
    } as;
};

/* 関数定義 */
typedef struct {
    char  *name;
    char **params;
    int    paramc;
    Block  body;
} FuncDef;

/* 文ノード */
struct Stmt {
    StmtKind kind;
    int      line;
    union {
        Node *expr;                          /* STMT_EXPR / STMT_RET(値、NULL可) */
        struct { bool is_local; char *name; Node *init; } var_decl;
        struct { char *name; Node *size; } hold;
        struct { char *name; } drop;
        struct { Node *target; Node *value; } assign;
        struct { Node *cond; Block then_blk; Block else_blk; bool has_else; } if_s;
        struct { Node *cond; Block body; } while_s;
        struct { Block pick_blk; Block error_blk; } pick_s;
        struct { char *module; char *alias; bool is_path; } join;
        struct { int version; } spc_ver;
        FuncDef func;
    } as;
};

/* プログラム全体 */
typedef struct {
    bool     has_file_decl;
    int      ich_ver;         /* -1: 未指定 */
    Block    stmts;           /* トップレベル文 (関数定義含む) */
} Program;

/* ブロック操作 */
void  block_init(Block *b);
void  block_push(Block *b, Stmt *s);

/* コンストラクタ */
Node *node_number(double v, int line);
Node *node_string(const char *s, int line);
Node *node_ident(const char *s, int line);
Node *node_var_ref(const char *s, int line);
Node *node_binary(int op, Node *l, Node *r, int line);
Node *node_unary(int op, Node *operand, int line);
Node *node_member(Node *obj, const char *name, int line);
Node *node_index(Node *obj, Node *idx, int line);
Node *node_spc_var(const char *name, int line);

void  node_free(Node *n);
void  stmt_free(Stmt *s);
void  program_free(Program *p);

#endif /* ICHIYANAGI_AST_H */
