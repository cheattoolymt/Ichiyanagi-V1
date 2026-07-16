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

#ifndef ICHIYANAGI_CODEGEN_H
#define ICHIYANAGI_CODEGEN_H

#include "ast.h"

/*
 * Ichiyanagi V1 ネイティブコード生成 (SPEC 13 章)。
 *
 * AST から x86-64 アセンブリ (Linux / System V AMD64 ABI) を自前生成する。
 * 生成物はテキストの GAS (AT&T 記法) アセンブリであり、Linux 純正の
 * `as` / `ld` に引き渡すことで ELF ネイティブ実行ファイルになる。
 *
 * V1 の対応スコープ (コンパイル可能な言語サブセット):
 *   - 数値 (64bit 整数モデル) / 真偽値 (0/1)
 *   - 算術 (+ - * /) / 比較 (gt lt eq neq ge le) / 論理 (not and or)
 *   - var / pvar 宣言・代入、グローバル値参照 (var x)
 *   - 関数定義・呼び出し・再帰・ret
 *   - if / else / while
 *   - terminal.print / terminal.printn (数値・文字列リテラル)
 *
 * 上記以外 (list/array, hold/drop, pick/error, 上記以外のモジュール) は
 * コンパイル非対応であり、その旨のエラーを返す (インタプリタ実行を案内)。
 */

/* コード生成結果コード */
typedef enum {
    CODEGEN_OK = 0,
    CODEGEN_UNSUPPORTED = 1,  /* コンパイル非対応の構文 */
    CODEGEN_ERROR = 2         /* その他の生成エラー */
} CodegenResult;

/*
 * AST(Program) から x86-64 アセンブリを生成し FILE* に書き出す。
 * 成功時 CODEGEN_OK。非対応構文があれば CODEGEN_UNSUPPORTED を返し、
 * 標準エラーへ理由を出力する。
 */
CodegenResult codegen_emit(Program *prog, FILE *out);

/*
 * 高水準ヘルパ: ソース由来の Program を受け取り、
 *   1) アセンブリを一時ファイルへ生成
 *   2) `as` でアセンブル
 *   3) `ld` でリンク
 * して out_path にネイティブ実行ファイルを生成する。
 *
 * emit_asm_only が true の場合はアセンブルせず、out_path に .s を残す。
 * 戻り値は 0 が成功、非 0 が失敗。
 */
int codegen_compile_to_file(Program *prog, const char *out_path,
                            bool emit_asm_only);

#endif /* ICHIYANAGI_CODEGEN_H */
