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

#include "value.h"
#include <math.h>

Value value_nil(void) { Value v; v.type = VAL_NIL; return v; }

Value value_number(double n) {
    Value v; v.type = VAL_NUMBER; v.as.num = n; return v;
}

Value value_bool(bool b) {
    Value v; v.type = VAL_BOOL; v.as.boolean = b; return v;
}

Value value_string_len(const char *s, int len) {
    Value v; v.type = VAL_STRING;
    ValString *vs = ich_malloc(sizeof(ValString));
    vs->refcount = 1;
    vs->len = len;
    vs->data = ich_malloc(len + 1);
    if (s && len > 0) memcpy(vs->data, s, len);
    vs->data[len] = '\0';
    v.as.str = vs;
    return v;
}

Value value_string(const char *s) {
    return value_string_len(s, s ? (int)strlen(s) : 0);
}

Value value_list(void) {
    Value v; v.type = VAL_LIST;
    ValArray *a = ich_malloc(sizeof(ValArray));
    a->refcount = 1; a->items = NULL; a->count = 0; a->cap = 0; a->fixed = false;
    v.as.arr = a;
    return v;
}

Value value_array(int size) {
    Value v; v.type = VAL_ARRAY;
    ValArray *a = ich_malloc(sizeof(ValArray));
    a->refcount = 1;
    a->count = size;
    a->cap = size > 0 ? size : 1;
    a->fixed = true;
    a->items = ich_malloc(sizeof(Value) * a->cap);
    for (int i = 0; i < size; i++) a->items[i] = value_nil();
    v.as.arr = a;
    return v;
}

Value value_module(int module_id) {
    Value v; v.type = VAL_MODULE; v.as.module_id = module_id; return v;
}

Value value_func(int func_id) {
    Value v; v.type = VAL_FUNC; v.as.func_id = func_id; return v;
}

Value value_heap(size_t size) {
    Value v; v.type = VAL_HEAP;
    ValHeap *h = ich_malloc(sizeof(ValHeap));
    h->refcount = 1;
    h->size = size;
    h->ptr = ich_malloc(size ? size : 1);
    memset(h->ptr, 0, size ? size : 1);
    v.as.heap = h;
    return v;
}

Value value_retain(Value v) {
    switch (v.type) {
        case VAL_STRING: v.as.str->refcount++; break;
        case VAL_LIST:
        case VAL_ARRAY:  v.as.arr->refcount++; break;
        case VAL_HEAP:   v.as.heap->refcount++; break;
        default: break;
    }
    return v;
}

void value_release(Value v) {
    switch (v.type) {
        case VAL_STRING:
            if (--v.as.str->refcount <= 0) {
                free(v.as.str->data);
                free(v.as.str);
            }
            break;
        case VAL_LIST:
        case VAL_ARRAY:
            if (--v.as.arr->refcount <= 0) {
                for (int i = 0; i < v.as.arr->count; i++) value_release(v.as.arr->items[i]);
                free(v.as.arr->items);
                free(v.as.arr);
            }
            break;
        case VAL_HEAP:
            if (--v.as.heap->refcount <= 0) {
                free(v.as.heap->ptr);
                free(v.as.heap);
            }
            break;
        default: break;
    }
}

void arr_push(ValArray *a, Value v) {
    if (a->count >= a->cap) {
        a->cap = a->cap ? a->cap * 2 : 4;
        a->items = ich_realloc(a->items, sizeof(Value) * a->cap);
    }
    a->items[a->count++] = value_retain(v);
}

Value arr_pop(ValArray *a) {
    if (a->count == 0) return value_nil();
    Value v = a->items[--a->count];
    /* 呼び出し側に所有権を渡す(retainしない) */
    return v;
}

Value arr_get(ValArray *a, int idx) {
    if (idx < 0 || idx >= a->count) return value_nil();
    return value_retain(a->items[idx]);
}

void arr_set(ValArray *a, int idx, Value v) {
    if (idx < 0) return;
    if (idx >= a->cap) {
        int nc = a->cap ? a->cap : 4;
        while (nc <= idx) nc *= 2;
        a->items = ich_realloc(a->items, sizeof(Value) * nc);
        for (int i = a->cap; i < nc; i++) a->items[i] = value_nil();
        a->cap = nc;
    }
    if (idx >= a->count) {
        for (int i = a->count; i < idx; i++) a->items[i] = value_nil();
        a->count = idx + 1;
    } else {
        value_release(a->items[idx]);
    }
    a->items[idx] = value_retain(v);
}

bool value_truthy(Value v) {
    switch (v.type) {
        case VAL_NIL:    return false;
        case VAL_BOOL:   return v.as.boolean;
        case VAL_NUMBER: return v.as.num != 0.0;
        case VAL_STRING: return v.as.str->len != 0;
        case VAL_LIST:
        case VAL_ARRAY:  return v.as.arr->count != 0;
        default:         return true;
    }
}

/* 数値の見やすい文字列化: 整数なら小数点なし */
static char *num_to_cstr(double n) {
    char buf[64];
    if (n == (long long)n && fabs(n) < 1e15) {
        snprintf(buf, sizeof(buf), "%lld", (long long)n);
    } else {
        snprintf(buf, sizeof(buf), "%g", n);
    }
    return ich_strdup(buf);
}

char *value_to_cstr(Value v) {
    switch (v.type) {
        case VAL_NIL:    return ich_strdup("nil");
        case VAL_BOOL:   return ich_strdup(v.as.boolean ? "true" : "false");
        case VAL_NUMBER: return num_to_cstr(v.as.num);
        case VAL_STRING: return ich_strdup(v.as.str->data);
        case VAL_MODULE: return ich_strdup("<module>");
        case VAL_FUNC:   return ich_strdup("<function>");
        case VAL_HEAP:   return ich_strdup("<heap>");
        case VAL_LIST:
        case VAL_ARRAY: {
            /* [a, b, c] 形式 */
            size_t cap = 32, len = 0;
            char *buf = ich_malloc(cap);
            buf[len++] = '[';
            for (int i = 0; i < v.as.arr->count; i++) {
                if (i > 0) {
                    if (len + 2 >= cap) { cap *= 2; buf = ich_realloc(buf, cap); }
                    buf[len++] = ','; buf[len++] = ' ';
                }
                char *s = value_to_cstr(v.as.arr->items[i]);
                bool is_str = v.as.arr->items[i].type == VAL_STRING;
                size_t sl = strlen(s);
                while (len + sl + 4 >= cap) { cap *= 2; buf = ich_realloc(buf, cap); }
                if (is_str) buf[len++] = '"';
                memcpy(buf + len, s, sl); len += sl;
                if (is_str) buf[len++] = '"';
                free(s);
            }
            if (len + 2 >= cap) { cap += 2; buf = ich_realloc(buf, cap); }
            buf[len++] = ']';
            buf[len] = '\0';
            return buf;
        }
    }
    return ich_strdup("");
}

bool value_equals(Value a, Value b) {
    if (a.type != b.type) {
        /* 数値とboolの緩い比較は行わない。厳密比較 */
        return false;
    }
    switch (a.type) {
        case VAL_NIL:    return true;
        case VAL_BOOL:   return a.as.boolean == b.as.boolean;
        case VAL_NUMBER: return a.as.num == b.as.num;
        case VAL_STRING: return a.as.str->len == b.as.str->len &&
                                memcmp(a.as.str->data, b.as.str->data, a.as.str->len) == 0;
        case VAL_LIST:
        case VAL_ARRAY:  return a.as.arr == b.as.arr;
        default:         return false;
    }
}

const char *value_type_name(Value v) {
    switch (v.type) {
        case VAL_NIL: return "nil";
        case VAL_NUMBER: return "number";
        case VAL_BOOL: return "bool";
        case VAL_STRING: return "string";
        case VAL_LIST: return "list";
        case VAL_ARRAY: return "array";
        case VAL_MODULE: return "module";
        case VAL_FUNC: return "function";
        case VAL_HEAP: return "heap";
        default: return "?";
    }
}
