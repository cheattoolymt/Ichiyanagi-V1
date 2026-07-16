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
#include "lexer.h"
#include "parser.h"
#include "interp.h"
#include "codegen.h"

#define ICH_VERSION_STR "1"
#define ICH_CODENAME    "Tega-Neson"

static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ichiyanagi: error: cannot open source file '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    char *buf = ich_malloc(sz + 1);
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Ichiyanagi V1 (codename: %s, ich-ver=%s)\n"
        "Copyright 2026 nyan<(nyan4)  -- Apache License 2.0\n\n"
        "Usage:\n"
        "  %s <source.ich> [program args...]        # interpret (tree-walk)\n"
        "  %s --compile <source.ich> [-o out]        # compile to native ELF (as/ld)\n"
        "  %s --emit-asm <source.ich> [-o out.s]     # emit x86-64 assembly only\n"
        "  %s --tokens <source.ich>                  # dump tokens (debug)\n"
        "  %s --version\n"
        "  %s --help\n",
        ICH_CODENAME, ICH_VERSION_STR, prog, prog, prog, prog, prog, prog);
}

/* out_path が未指定のとき src からデフォルト名を作る。
 * --emit-asm なら .s、--compile なら拡張子除去 (無ければ .out)。要 free。 */
static char *default_output_path(const char *src, bool asm_only) {
    const char *base = src;
    const char *slash = strrchr(src, '/');
    if (slash) base = slash + 1;
    size_t len = strlen(base);
    /* 拡張子 .ich を除去 */
    const char *dot = strrchr(base, '.');
    size_t stem = dot ? (size_t)(dot - base) : len;
    char *out = ich_malloc(stem + 8);
    memcpy(out, base, stem);
    out[stem] = '\0';
    if (asm_only) strcat(out, ".s");
    else if (stem == len) strcat(out, ".out"); /* 元々拡張子なし */
    return out;
}

static int run_codegen(const char *src_path, const char *out_opt, bool asm_only) {
    char *src = read_whole_file(src_path);
    if (!src) return 1;
    TokenList tl = lexer_tokenize(src);
    Program prog = parser_parse(&tl);

    char *out_path = out_opt ? ich_strdup(out_opt)
                             : default_output_path(src_path, asm_only);
    int rc = codegen_compile_to_file(&prog, out_path, asm_only);

    free(out_path);
    program_free(&prog);
    tokenlist_free(&tl);
    free(src);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("Ichiyanagi V1 (codename: %s, ich-ver=%s)\n", ICH_CODENAME, ICH_VERSION_STR);
        return 0;
    }

    /* コンパイルモード: --compile / --emit-asm */
    if (strcmp(argv[1], "--compile") == 0 || strcmp(argv[1], "-c") == 0 ||
        strcmp(argv[1], "--emit-asm") == 0 || strcmp(argv[1], "-S") == 0) {
        bool asm_only = (strcmp(argv[1], "--emit-asm") == 0 ||
                         strcmp(argv[1], "-S") == 0);
        const char *src_path = NULL;
        const char *out_opt = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                if (i + 1 >= argc) { print_usage(argv[0]); return 1; }
                out_opt = argv[++i];
            } else if (!src_path) {
                src_path = argv[i];
            }
        }
        if (!src_path) { print_usage(argv[0]); return 1; }
        return run_codegen(src_path, out_opt, asm_only);
    }

    bool dump_tokens = false;
    const char *src_path = NULL;
    int prog_arg_start = 2;

    if (strcmp(argv[1], "--tokens") == 0) {
        if (argc < 3) { print_usage(argv[0]); return 1; }
        dump_tokens = true;
        src_path = argv[2];
        prog_arg_start = 3;
    } else {
        src_path = argv[1];
        prog_arg_start = 2;
    }

    char *src = read_whole_file(src_path);
    if (!src) return 1;

    TokenList tl = lexer_tokenize(src);

    if (dump_tokens) {
        for (int i = 0; i < tl.count; i++) {
            Token *t = &tl.tokens[i];
            printf("[%3d] line %-3d  %-10s", i, t->line, token_type_name(t->type));
            if (t->text) printf("  '%s'", t->text);
            if (t->type == TK_NUMBER) printf("  (num=%g)", t->num);
            printf("\n");
        }
        tokenlist_free(&tl);
        free(src);
        return 0;
    }

    Program prog = parser_parse(&tl);

    /* プログラムへ渡す argv: [source.ich, program args...] */
    int p_argc = argc - prog_arg_start + 1;
    char **p_argv = ich_malloc(sizeof(char *) * (p_argc + 1));
    p_argv[0] = (char *)src_path;
    for (int i = 0; i < argc - prog_arg_start; i++) {
        p_argv[i + 1] = argv[prog_arg_start + i];
    }
    p_argv[p_argc] = NULL;

    int rc = interp_run(&prog, p_argc, p_argv);

    free(p_argv);
    program_free(&prog);
    tokenlist_free(&tl);
    free(src);
    return rc;
}
