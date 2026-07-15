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

#ifndef ICHIYANAGI_VALUE_H
#define ICHIYANAGI_VALUE_H

#include "common.h"

typedef enum {
    VAL_NIL,
    VAL_NUMBER,
    VAL_BOOL,
    VAL_STRING,
    VAL_LIST,     /* 可変長 list() */
    VAL_ARRAY,    /* 固定長 array() */
    VAL_MODULE,   /* 標準モジュール参照 */
    VAL_FUNC,     /* ユーザー定義関数(名前解決用) */
    VAL_HEAP      /* hold で確保したヒープ領域 */
} ValueType;

typedef struct Value Value;

/* 参照カウント付きの配列/文字列のバッキング */
typedef struct {
    int    refcount;
    Value *items;
    int    count;
    int    cap;
    bool   fixed;    /* array のとき true */
} ValArray;

typedef struct {
    int   refcount;
    char *data;
    int   len;
} ValString;

typedef struct {
    int    refcount;
    void  *ptr;
    size_t size;
} ValHeap;

struct Value {
    ValueType type;
    union {
        double     num;
        bool       boolean;
        ValString *str;
        ValArray  *arr;
        int        module_id;  /* module.h の MOD_* */
        int        func_id;    /* env 側の関数テーブル index */
        ValHeap   *heap;
    } as;
};

/* コンストラクタ */
Value value_nil(void);
Value value_number(double n);
Value value_bool(bool b);
Value value_string(const char *s);
Value value_string_len(const char *s, int len);
Value value_list(void);
Value value_array(int size);
Value value_module(int module_id);
Value value_func(int func_id);
Value value_heap(size_t size);

/* 参照管理 */
Value value_retain(Value v);
void  value_release(Value v);

/* 配列操作 */
void   arr_push(ValArray *a, Value v);
Value  arr_pop(ValArray *a);
Value  arr_get(ValArray *a, int idx);
void   arr_set(ValArray *a, int idx, Value v);

/* ユーティリティ */
bool   value_truthy(Value v);
char  *value_to_cstr(Value v);   /* 表示用文字列(要free) */
bool   value_equals(Value a, Value b);
const char *value_type_name(Value v);

#endif /* ICHIYANAGI_VALUE_H */
