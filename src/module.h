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

#ifndef ICHIYANAGI_MODULE_H
#define ICHIYANAGI_MODULE_H

#include "value.h"
#include "ast.h"

typedef struct Interp Interp;

/* 標準モジュール ID */
typedef enum {
    MOD_TERMINAL = 1,
    MOD_MATH,
    MOD_STRING,
    MOD_FILE,
    MOD_TIME,
    MOD_SYS,
    MOD_UNKNOWN = 0
} ModuleId;

/* モジュール名 -> ID (未知なら MOD_UNKNOWN) */
ModuleId module_lookup(const char *name);

/* モジュールメソッド呼び出し。
 * argnames は名前付き引数対応(terminal.input の answer= 用)。
 * call_node は answer= の代入先変数名を得るため渡す。 */
Value module_call(Interp *it, ModuleId mod, const char *method,
                  Value *args, char **argnames, int argc, Node *call_node);

/* list()/array() などのグローバル組み込み関数呼び出し。
 * 見つからなければ handled=false を返す。 */
Value builtin_call(Interp *it, const char *name,
                   Value *args, int argc, bool *handled);

/* list/array のメソッド (push/pop/len) */
Value value_method_call(Interp *it, Value target, const char *method,
                        Value *args, int argc, bool *handled);

#endif /* ICHIYANAGI_MODULE_H */
