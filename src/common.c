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

#include "common.h"
#include <stdarg.h>

void *ich_malloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) {
        fprintf(stderr, "ichiyanagi: fatal: out of memory\n");
        exit(1);
    }
    return p;
}

void *ich_realloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size ? size : 1);
    if (!p) {
        fprintf(stderr, "ichiyanagi: fatal: out of memory\n");
        exit(1);
    }
    return p;
}

char *ich_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = ich_malloc(n);
    memcpy(p, s, n);
    return p;
}

void ich_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "ichiyanagi: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}
