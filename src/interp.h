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

#ifndef ICHIYANAGI_INTERP_H
#define ICHIYANAGI_INTERP_H

#include "ast.h"
#include "value.h"
#include "error.h"

/* 変数エントリ */
typedef struct {
    char *name;
    Value value;
} VarEntry;

/* スコープ(グローバル or 関数ローカル) */
typedef struct Scope {
    VarEntry     *vars;
    int           count;
    int           cap;
    struct Scope *parent;   /* NULL ならグローバル */
} Scope;

/* 関数テーブルエントリ */
typedef struct {
    char    *name;
    FuncDef *def;    /* AST を参照(所有しない) */
} FuncEntry;

/* インタプリタ状態 */
typedef struct Interp {
    Scope      globals;
    Scope     *current;       /* 現在のローカルスコープ(グローバル実行時は &globals) */
    FuncEntry *funcs;
    int        func_count;
    int        func_cap;

    /* sys.args 用 */
    int    argc;
    char **argv;

    /* ret 制御 */
    bool   returning;
    Value  return_value;
} Interp;

/* 実行エントリ */
int interp_run(Program *prog, int argc, char **argv);

/* 内部: 式評価 / 文実行 (module.c から呼ぶため公開) */
Value interp_eval(Interp *it, Node *n);
void  interp_exec_block(Interp *it, Block *b);

/* 変数アクセス */
Value scope_get(Interp *it, const char *name, bool *found);
void  scope_set_existing_or_global(Interp *it, const char *name, Value v);
void  scope_declare(Scope *s, const char *name, Value v);

#endif /* ICHIYANAGI_INTERP_H */
