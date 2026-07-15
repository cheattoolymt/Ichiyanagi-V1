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

#include "lexer.h"
#include <ctype.h>

typedef struct {
    const char *src;
    size_t      pos;
    int         line;
    TokenList   out;
} Lexer;

static void push_token(Lexer *lx, TokenType type, char *text, double num) {
    if (lx->out.count >= lx->out.cap) {
        lx->out.cap = lx->out.cap ? lx->out.cap * 2 : 64;
        lx->out.tokens = ich_realloc(lx->out.tokens, sizeof(Token) * lx->out.cap);
    }
    Token *t = &lx->out.tokens[lx->out.count++];
    t->type = type;
    t->text = text;
    t->num  = num;
    t->line = lx->line;
}

static char peek(Lexer *lx)  { return lx->src[lx->pos]; }
static char peek2(Lexer *lx) { return lx->src[lx->pos] ? lx->src[lx->pos + 1] : '\0'; }
static char advance(Lexer *lx) { return lx->src[lx->pos++]; }

/* キーワード判定 */
static TokenType keyword_type(const char *s) {
    struct { const char *kw; TokenType t; } kws[] = {
        {"spc", TK_SPC}, {"var", TK_VAR}, {"pvar", TK_PVAR},
        {"hold", TK_HOLD}, {"drop", TK_DROP}, {"join", TK_JOIN},
        {"as", TK_AS}, {"exec", TK_EXEC}, {"end", TK_END},
        {"if", TK_IF}, {"else", TK_ELSE}, {"while", TK_WHILE},
        {"ret", TK_RET}, {"pick", TK_PICK}, {"error", TK_ERROR},
        {"gt", TK_GT}, {"lt", TK_LT}, {"eq", TK_EQ}, {"neq", TK_NEQ},
        {"ge", TK_GE}, {"le", TK_LE},
        {"not", TK_NOT}, {"and", TK_AND}, {"or", TK_OR},
        {NULL, TK_EOF}
    };
    for (int i = 0; kws[i].kw; i++) {
        if (strcmp(s, kws[i].kw) == 0) return kws[i].t;
    }
    return TK_IDENT;
}

/* 識別子に使える文字: 英数字, '_', '-'(ich-ver等), '.'(モジュールアクセスは別処理) */
static bool ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}
static bool ident_part(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-';
}

/* 文字列リテラル読み込み。quoteは ' か " */
static char *read_string(Lexer *lx, char quote) {
    size_t start = lx->pos;
    size_t cap = 16, len = 0;
    char *buf = ich_malloc(cap);
    while (peek(lx) && peek(lx) != quote) {
        char c = advance(lx);
        if (c == '\\') {
            /* エスケープ処理 */
            char n = peek(lx);
            char out = 0;
            switch (n) {
                case 'n': out = '\n'; advance(lx); break;
                case 't': out = '\t'; advance(lx); break;
                case 'r': out = '\r'; advance(lx); break;
                case '\\': out = '\\'; advance(lx); break;
                case '"': out = '"'; advance(lx); break;
                case '\'': out = '\''; advance(lx); break;
                case '0': out = '\0'; advance(lx); break;
                default: out = c; break; /* '\' そのまま */
            }
            if (len + 1 >= cap) { cap *= 2; buf = ich_realloc(buf, cap); }
            buf[len++] = out;
        } else {
            if (c == '\n') lx->line++;
            if (len + 1 >= cap) { cap *= 2; buf = ich_realloc(buf, cap); }
            buf[len++] = c;
        }
    }
    if (peek(lx) != quote) {
        ich_fatal("line %d: unterminated string literal", lx->line);
    }
    advance(lx); /* 終端 quote */
    buf[len] = '\0';
    (void)start;
    return buf;
}

TokenList lexer_tokenize(const char *src) {
    Lexer lx = {0};
    lx.src = src;
    lx.pos = 0;
    lx.line = 1;

    while (peek(&lx)) {
        char c = peek(&lx);

        /* ファイル宣言 !"..." */
        if (c == '!' && peek2(&lx) == '"') {
            advance(&lx); /* ! */
            advance(&lx); /* " */
            char *s = read_string(&lx, '"');
            push_token(&lx, TK_FILE_DECL, s, 0);
            continue;
        }

        /* 複数行コメント \" ... " */
        if (c == '\\' && peek2(&lx) == '"') {
            advance(&lx); /* \ */
            advance(&lx); /* " */
            while (peek(&lx) && peek(&lx) != '"') {
                if (peek(&lx) == '\n') lx.line++;
                advance(&lx);
            }
            if (peek(&lx) == '"') advance(&lx);
            continue;
        }

        /* 単一行コメント \ ... 行末 */
        if (c == '\\') {
            while (peek(&lx) && peek(&lx) != '\n') advance(&lx);
            continue;
        }

        /* 改行 */
        if (c == '\n') {
            advance(&lx);
            lx.line++;
            push_token(&lx, TK_NEWLINE, NULL, 0);
            continue;
        }

        /* 空白(改行以外) */
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(&lx);
            continue;
        }

        /* 文字列リテラル */
        if (c == '"' || c == '\'') {
            advance(&lx);
            char *s = read_string(&lx, c);
            push_token(&lx, TK_STRING, s, 0);
            continue;
        }

        /* 数値 */
        if (isdigit((unsigned char)c) ||
            (c == '.' && isdigit((unsigned char)peek2(&lx)))) {
            size_t start = lx.pos;
            while (isdigit((unsigned char)peek(&lx))) advance(&lx);
            if (peek(&lx) == '.' && isdigit((unsigned char)peek2(&lx))) {
                advance(&lx);
                while (isdigit((unsigned char)peek(&lx))) advance(&lx);
            }
            size_t n = lx.pos - start;
            char *txt = ich_malloc(n + 1);
            memcpy(txt, lx.src + start, n);
            txt[n] = '\0';
            push_token(&lx, TK_NUMBER, txt, atof(txt));
            continue;
        }

        /* 識別子 / キーワード */
        if (ident_start(c)) {
            size_t start = lx.pos;
            while (ident_part(peek(&lx))) advance(&lx);
            size_t n = lx.pos - start;
            char *txt = ich_malloc(n + 1);
            memcpy(txt, lx.src + start, n);
            txt[n] = '\0';
            TokenType kt = keyword_type(txt);
            push_token(&lx, kt, txt, 0);
            continue;
        }

        /* 記号 */
        advance(&lx);
        switch (c) {
            case '+': push_token(&lx, TK_PLUS, NULL, 0); break;
            case '-': push_token(&lx, TK_MINUS, NULL, 0); break;
            case '*': push_token(&lx, TK_STAR, NULL, 0); break;
            case '/': push_token(&lx, TK_SLASH, NULL, 0); break;
            case '=': push_token(&lx, TK_ASSIGN, NULL, 0); break;
            case '(': push_token(&lx, TK_LPAREN, NULL, 0); break;
            case ')': push_token(&lx, TK_RPAREN, NULL, 0); break;
            case '[': push_token(&lx, TK_LBRACKET, NULL, 0); break;
            case ']': push_token(&lx, TK_RBRACKET, NULL, 0); break;
            case ',': push_token(&lx, TK_COMMA, NULL, 0); break;
            case ':': push_token(&lx, TK_COLON, NULL, 0); break;
            case '.': push_token(&lx, TK_DOT, NULL, 0); break;
            default:
                ich_fatal("line %d: unexpected character '%c' (0x%02x)",
                          lx.line, c, (unsigned char)c);
        }
    }

    push_token(&lx, TK_EOF, NULL, 0);
    return lx.out;
}

void tokenlist_free(TokenList *tl) {
    if (!tl || !tl->tokens) return;
    for (int i = 0; i < tl->count; i++) {
        free(tl->tokens[i].text);
    }
    free(tl->tokens);
    tl->tokens = NULL;
    tl->count = tl->cap = 0;
}

const char *token_type_name(TokenType t) {
    switch (t) {
        case TK_NUMBER: return "NUMBER";
        case TK_STRING: return "STRING";
        case TK_IDENT: return "IDENT";
        case TK_FILE_DECL: return "FILE_DECL";
        case TK_SPC: return "spc"; case TK_VAR: return "var";
        case TK_PVAR: return "pvar"; case TK_HOLD: return "hold";
        case TK_DROP: return "drop"; case TK_JOIN: return "join";
        case TK_AS: return "as"; case TK_EXEC: return "exec";
        case TK_END: return "end"; case TK_IF: return "if";
        case TK_ELSE: return "else"; case TK_WHILE: return "while";
        case TK_RET: return "ret"; case TK_PICK: return "pick";
        case TK_ERROR: return "error";
        case TK_GT: return "gt"; case TK_LT: return "lt";
        case TK_EQ: return "eq"; case TK_NEQ: return "neq";
        case TK_GE: return "ge"; case TK_LE: return "le";
        case TK_NOT: return "not"; case TK_AND: return "and"; case TK_OR: return "or";
        case TK_PLUS: return "+"; case TK_MINUS: return "-";
        case TK_STAR: return "*"; case TK_SLASH: return "/";
        case TK_ASSIGN: return "="; case TK_LPAREN: return "(";
        case TK_RPAREN: return ")"; case TK_LBRACKET: return "[";
        case TK_RBRACKET: return "]"; case TK_COMMA: return ",";
        case TK_COLON: return ":"; case TK_DOT: return ".";
        case TK_NEWLINE: return "NEWLINE"; case TK_EOF: return "EOF";
        default: return "?";
    }
}
