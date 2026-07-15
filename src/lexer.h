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

#ifndef ICHIYANAGI_LEXER_H
#define ICHIYANAGI_LEXER_H

#include "common.h"

typedef enum {
    /* リテラル・識別子 */
    TK_NUMBER,      /* 数値リテラル */
    TK_STRING,      /* 文字列リテラル */
    TK_IDENT,       /* 識別子 */

    /* ファイル宣言 */
    TK_FILE_DECL,   /* !"Ichiyanagi Code" */

    /* キーワード */
    TK_SPC,         /* spc */
    TK_VAR,         /* var */
    TK_PVAR,        /* pvar */
    TK_HOLD,        /* hold */
    TK_DROP,        /* drop */
    TK_JOIN,        /* join */
    TK_AS,          /* as */
    TK_EXEC,        /* exec */
    TK_END,         /* end */
    TK_IF,          /* if */
    TK_ELSE,        /* else */
    TK_WHILE,       /* while */
    TK_RET,         /* ret */
    TK_PICK,        /* pick */
    TK_ERROR,       /* error */

    /* 比較演算子 (英単語) */
    TK_GT, TK_LT, TK_EQ, TK_NEQ, TK_GE, TK_LE,
    /* 論理演算子 */
    TK_NOT, TK_AND, TK_OR,

    /* 記号 */
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH,
    TK_ASSIGN,      /* = */
    TK_LPAREN, TK_RPAREN,
    TK_LBRACKET, TK_RBRACKET,
    TK_COMMA,
    TK_COLON,       /* : */
    TK_DOT,         /* . */

    TK_NEWLINE,     /* 文の区切り */
    TK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char     *text;   /* 文字列/識別子/数値の生テキスト。所有 */
    double    num;    /* 数値のとき */
    int       line;
} Token;

typedef struct {
    Token *tokens;
    int    count;
    int    cap;
} TokenList;

/* ソースをトークン列に変換する */
TokenList lexer_tokenize(const char *src);
void      tokenlist_free(TokenList *tl);
const char *token_type_name(TokenType t);

#endif /* ICHIYANAGI_LEXER_H */
