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

#define _POSIX_C_SOURCE 200809L
#include "module.h"
#include "interp.h"
#include "error.h"
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

ModuleId module_lookup(const char *name) {
    if (strcmp(name, "terminal") == 0) return MOD_TERMINAL;
    if (strcmp(name, "math") == 0)     return MOD_MATH;
    if (strcmp(name, "string") == 0)   return MOD_STRING;
    if (strcmp(name, "file") == 0)     return MOD_FILE;
    if (strcmp(name, "time") == 0)     return MOD_TIME;
    if (strcmp(name, "sys") == 0)      return MOD_SYS;
    return MOD_UNKNOWN;
}

static double arg_num(Value *args, int argc, int i) {
    if (i >= argc) err_raise(ERR_ARGUMENT, "missing argument %d", i + 1);
    Value v = args[i];
    if (v.type == VAL_NUMBER) return v.as.num;
    if (v.type == VAL_BOOL)   return v.as.boolean ? 1 : 0;
    err_raise(ERR_TYPE, "argument %d must be a number", i + 1);
    return 0;
}

static const char *arg_str(Value *args, int argc, int i) {
    if (i >= argc) err_raise(ERR_ARGUMENT, "missing argument %d", i + 1);
    if (args[i].type != VAL_STRING)
        err_raise(ERR_TYPE, "argument %d must be a string", i + 1);
    return args[i].as.str->data;
}

/* ---- terminal ---- */
static Value mod_terminal(Interp *it, const char *method,
                          Value *args, char **argnames, int argc, Node *call_node) {
    if (strcmp(method, "print") == 0) {
        for (int i = 0; i < argc; i++) {
            char *s = value_to_cstr(args[i]);
            fputs(s, stdout);
            free(s);
        }
        fputc('\n', stdout);
        return value_nil();
    }
    if (strcmp(method, "printn") == 0) {
        for (int i = 0; i < argc; i++) {
            char *s = value_to_cstr(args[i]);
            fputs(s, stdout);
            free(s);
        }
        fflush(stdout);
        return value_nil();
    }
    if (strcmp(method, "input") == 0) {
        /* terminal.input(prompt, answer=var) */
        if (argc >= 1) {
            char *p = value_to_cstr(args[0]);
            fputs(p, stdout);
            free(p);
            fflush(stdout);
        }
        char buf[4096];
        char *line = fgets(buf, sizeof(buf), stdin);
        Value result;
        if (!line) {
            result = value_string("");
        } else {
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
            if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';
            result = value_string(buf);
        }
        /* answer= で指定された変数へ格納 */
        for (int i = 0; i < argc; i++) {
            if (argnames && argnames[i] && strcmp(argnames[i], "answer") == 0) {
                /* args[i] は評価済みだが、格納先は call_node の該当引数の名前 */
                /* call_node.args[i] は代入先変数を表す式(IDENT想定) */
                Node *tgt = call_node->as.call.args[i];
                if (tgt->kind == NODE_IDENT) {
                    scope_set_existing_or_global(it, tgt->as.ident.name, result);
                } else if (tgt->kind == NODE_VAR_REF) {
                    scope_set_existing_or_global(it, tgt->as.var_ref.name, result);
                }
            }
        }
        return result;
    }
    err_raise(ERR_NAME, "terminal has no method '%s'", method);
    return value_nil();
}

/* ---- math ---- */
static Value mod_math(const char *method, Value *args, int argc) {
    if (strcmp(method, "sqrt") == 0)  return value_number(sqrt(arg_num(args, argc, 0)));
    if (strcmp(method, "pow") == 0)   return value_number(pow(arg_num(args, argc, 0), arg_num(args, argc, 1)));
    if (strcmp(method, "abs") == 0)   return value_number(fabs(arg_num(args, argc, 0)));
    if (strcmp(method, "floor") == 0) return value_number(floor(arg_num(args, argc, 0)));
    if (strcmp(method, "ceil") == 0)  return value_number(ceil(arg_num(args, argc, 0)));
    err_raise(ERR_NAME, "math has no method '%s'", method);
    return value_nil();
}

/* ---- string ---- */
static Value mod_string(const char *method, Value *args, int argc) {
    if (strcmp(method, "len") == 0) {
        return value_number((double)strlen(arg_str(args, argc, 0)));
    }
    if (strcmp(method, "upper") == 0) {
        const char *s = arg_str(args, argc, 0);
        int n = (int)strlen(s);
        Value r = value_string_len(s, n);
        for (int i = 0; i < n; i++) r.as.str->data[i] = (char)toupper((unsigned char)r.as.str->data[i]);
        return r;
    }
    if (strcmp(method, "lower") == 0) {
        const char *s = arg_str(args, argc, 0);
        int n = (int)strlen(s);
        Value r = value_string_len(s, n);
        for (int i = 0; i < n; i++) r.as.str->data[i] = (char)tolower((unsigned char)r.as.str->data[i]);
        return r;
    }
    if (strcmp(method, "trim") == 0) {
        const char *s = arg_str(args, argc, 0);
        const char *start = s;
        while (*start && isspace((unsigned char)*start)) start++;
        const char *end = s + strlen(s);
        while (end > start && isspace((unsigned char)*(end - 1))) end--;
        return value_string_len(start, (int)(end - start));
    }
    if (strcmp(method, "split") == 0) {
        const char *s = arg_str(args, argc, 0);
        const char *sep = arg_str(args, argc, 1);
        Value list = value_list();
        int seplen = (int)strlen(sep);
        if (seplen == 0) {
            /* 1文字ずつ */
            for (const char *p = s; *p; p++) {
                Value c = value_string_len(p, 1);
                arr_push(list.as.arr, c);
                value_release(c);
            }
            return list;
        }
        const char *cur = s;
        const char *found;
        while ((found = strstr(cur, sep)) != NULL) {
            Value part = value_string_len(cur, (int)(found - cur));
            arr_push(list.as.arr, part);
            value_release(part);
            cur = found + seplen;
        }
        Value last = value_string(cur);
        arr_push(list.as.arr, last);
        value_release(last);
        return list;
    }
    err_raise(ERR_NAME, "string has no method '%s'", method);
    return value_nil();
}

/* ---- file ---- */
static Value mod_file(const char *method, Value *args, int argc) {
    if (strcmp(method, "read") == 0) {
        const char *path = arg_str(args, argc, 0);
        FILE *f = fopen(path, "rb");
        if (!f) err_raise(ERR_IO, "cannot open file '%s' for reading", path);
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) sz = 0;
        char *buf = ich_malloc(sz + 1);
        size_t rd = fread(buf, 1, sz, f);
        buf[rd] = '\0';
        fclose(f);
        Value r = value_string_len(buf, (int)rd);
        free(buf);
        return r;
    }
    if (strcmp(method, "write") == 0) {
        const char *path = arg_str(args, argc, 0);
        const char *content = arg_str(args, argc, 1);
        FILE *f = fopen(path, "wb");
        if (!f) err_raise(ERR_IO, "cannot open file '%s' for writing", path);
        fwrite(content, 1, strlen(content), f);
        fclose(f);
        return value_nil();
    }
    err_raise(ERR_NAME, "file has no method '%s'", method);
    return value_nil();
}

/* ---- time ---- */
static Value mod_time(const char *method, Value *args, int argc) {
    if (strcmp(method, "now") == 0) {
        return value_number((double)time(NULL));
    }
    if (strcmp(method, "sleep") == 0) {
        double sec = arg_num(args, argc, 0);
        struct timespec ts;
        ts.tv_sec = (time_t)sec;
        ts.tv_nsec = (long)((sec - (double)ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
        return value_nil();
    }
    err_raise(ERR_NAME, "time has no method '%s'", method);
    return value_nil();
}

/* ---- sys ---- */
static Value mod_sys(Interp *it, const char *method, Value *args, int argc) {
    if (strcmp(method, "args") == 0) {
        Value list = value_list();
        for (int i = 0; i < it->argc; i++) {
            Value s = value_string(it->argv[i]);
            arr_push(list.as.arr, s);
            value_release(s);
        }
        return list;
    }
    if (strcmp(method, "env") == 0) {
        const char *name = arg_str(args, argc, 0);
        const char *val = getenv(name);
        return value_string(val ? val : "");
    }
    if (strcmp(method, "exit") == 0) {
        int code = argc >= 1 ? (int)arg_num(args, argc, 0) : 0;
        exit(code);
    }
    err_raise(ERR_NAME, "sys has no method '%s'", method);
    return value_nil();
}

Value module_call(Interp *it, ModuleId mod, const char *method,
                  Value *args, char **argnames, int argc, Node *call_node) {
    switch (mod) {
        case MOD_TERMINAL: return mod_terminal(it, method, args, argnames, argc, call_node);
        case MOD_MATH:     return mod_math(method, args, argc);
        case MOD_STRING:   return mod_string(method, args, argc);
        case MOD_FILE:     return mod_file(method, args, argc);
        case MOD_TIME:     return mod_time(method, args, argc);
        case MOD_SYS:      return mod_sys(it, method, args, argc);
        default:
            err_raise(ERR_NAME, "unknown module");
    }
    return value_nil();
}

/* ---- グローバル組み込み: list() / array() ---- */
Value builtin_call(Interp *it, const char *name, Value *args, int argc, bool *handled) {
    (void)it;
    *handled = true;
    if (strcmp(name, "list") == 0) {
        Value list = value_list();
        for (int i = 0; i < argc; i++) arr_push(list.as.arr, args[i]);
        return list;
    }
    if (strcmp(name, "array") == 0) {
        int size = argc >= 1 ? (int)arg_num(args, argc, 0) : 0;
        if (size < 0) err_raise(ERR_VALUE, "array size must be >= 0");
        return value_array(size);
    }
    *handled = false;
    return value_nil();
}

/* ---- list/array のメソッド: push / pop / len ---- */
Value value_method_call(Interp *it, Value target, const char *method,
                        Value *args, int argc, bool *handled) {
    (void)it;
    *handled = true;
    if (target.type == VAL_LIST || target.type == VAL_ARRAY) {
        if (strcmp(method, "len") == 0) {
            return value_number((double)target.as.arr->count);
        }
        if (strcmp(method, "push") == 0) {
            if (target.type == VAL_ARRAY)
                err_raise(ERR_TYPE, "cannot push to a fixed-size array");
            for (int i = 0; i < argc; i++) arr_push(target.as.arr, args[i]);
            return value_nil();
        }
        if (strcmp(method, "pop") == 0) {
            if (target.type == VAL_ARRAY)
                err_raise(ERR_TYPE, "cannot pop from a fixed-size array");
            if (target.as.arr->count == 0)
                err_raise(ERR_INDEX, "pop from empty list");
            return arr_pop(target.as.arr);
        }
        err_raise(ERR_NAME, "list/array has no method '%s'", method);
    }
    *handled = false;
    return value_nil();
}
