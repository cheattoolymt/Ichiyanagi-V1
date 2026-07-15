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

#include "error.h"
#include <stdarg.h>

static ErrorFrame *g_top = NULL;
static ErrorInfo   g_current = { ERR_NONE, NULL };

void err_push_frame(ErrorFrame *f) {
    f->prev = g_top;
    g_top = f;
}

void err_pop_frame(void) {
    if (g_top) g_top = g_top->prev;
}

ErrorFrame *err_top_frame(void) {
    return g_top;
}

void err_set_current(ErrorCode code, const char *msg) {
    free(g_current.message);
    g_current.code = code;
    g_current.message = msg ? ich_strdup(msg) : NULL;
}

void err_clear_current(void) {
    free(g_current.message);
    g_current.message = NULL;
    g_current.code = ERR_NONE;
}

const char *err_current_message(void) {
    return g_current.message ? g_current.message : "";
}

ErrorCode err_current_code(void) {
    return g_current.code;
}

_Noreturn void err_raise(ErrorCode code, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    err_set_current(code, buf);

    ErrorFrame *f = g_top;
    if (f) {
        /* pop してから longjmp。ハンドラ側で処理させる */
        g_top = f->prev;
        longjmp(f->env, 1);
    } else {
        /* 捕捉されない実行時エラー */
        fprintf(stderr, "ichiyanagi: runtime error [code %d]: %s\n", code, buf);
        exit(1);
    }
}
