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

#ifndef ICHIYANAGI_ERROR_H
#define ICHIYANAGI_ERROR_H

#include "common.h"
#include <setjmp.h>

/* エラーコード体系 (spc var err-code) */
typedef enum {
    ERR_NONE          = 0,
    ERR_DIV_ZERO      = 1,   /* ゼロ除算 */
    ERR_TYPE          = 2,   /* 型不一致 */
    ERR_INDEX         = 3,   /* 添字範囲外 */
    ERR_NAME          = 4,   /* 未定義の名前 */
    ERR_VALUE         = 5,   /* 不正な値 */
    ERR_IO            = 6,   /* 入出力エラー */
    ERR_ARGUMENT      = 7,   /* 引数エラー */
    ERR_RUNTIME       = 99   /* その他 */
} ErrorCode;

/* pick ブロックのエラーハンドラをスタックで管理 */
typedef struct ErrorFrame {
    jmp_buf             env;
    struct ErrorFrame  *prev;
} ErrorFrame;

/* 現在のエラー情報 */
typedef struct {
    ErrorCode   code;
    char       *message;   /* 所有 */
} ErrorInfo;

/* ハンドラスタック操作 */
void        err_push_frame(ErrorFrame *f);
void        err_pop_frame(void);
ErrorFrame *err_top_frame(void);

/* エラー送出。ハンドラがあれば longjmp、無ければプログラム終了 */
_Noreturn void err_raise(ErrorCode code, const char *fmt, ...);

/* 現在のエラー情報 */
const char *err_current_message(void);
ErrorCode   err_current_code(void);
void        err_set_current(ErrorCode code, const char *msg);
void        err_clear_current(void);

#endif /* ICHIYANAGI_ERROR_H */
