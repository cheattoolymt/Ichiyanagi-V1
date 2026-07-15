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

#ifndef ICHIYANAGI_COMMON_H
#define ICHIYANAGI_COMMON_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define ICH_VERSION 1

/* 汎用: メモリ確保 (失敗時 abort) */
void *ich_malloc(size_t size);
void *ich_realloc(void *ptr, size_t size);
char *ich_strdup(const char *s);

/* エラー終了 */
void ich_fatal(const char *fmt, ...);

#endif /* ICHIYANAGI_COMMON_H */
