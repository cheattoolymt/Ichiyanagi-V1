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

#include "ast.h"

void block_init(Block *b) {
    b->items = NULL;
    b->count = 0;
    b->cap = 0;
}

void block_push(Block *b, Stmt *s) {
    if (b->count >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->items = ich_realloc(b->items, sizeof(Stmt *) * b->cap);
    }
    b->items[b->count++] = s;
}

static Node *node_new(NodeKind k, int line) {
    Node *n = ich_malloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->kind = k;
    n->line = line;
    return n;
}

Node *node_number(double v, int line) {
    Node *n = node_new(NODE_NUMBER, line);
    n->as.num = v;
    return n;
}

Node *node_string(const char *s, int line) {
    Node *n = node_new(NODE_STRING, line);
    n->as.str = ich_strdup(s);
    return n;
}

Node *node_ident(const char *s, int line) {
    Node *n = node_new(NODE_IDENT, line);
    n->as.ident.name = ich_strdup(s);
    return n;
}

Node *node_var_ref(const char *s, int line) {
    Node *n = node_new(NODE_VAR_REF, line);
    n->as.var_ref.name = ich_strdup(s);
    return n;
}

Node *node_binary(int op, Node *l, Node *r, int line) {
    Node *n = node_new(NODE_BINARY, line);
    n->as.binary.op = op;
    n->as.binary.l = l;
    n->as.binary.r = r;
    return n;
}

Node *node_unary(int op, Node *operand, int line) {
    Node *n = node_new(NODE_UNARY, line);
    n->as.unary.op = op;
    n->as.unary.operand = operand;
    return n;
}

Node *node_member(Node *obj, const char *name, int line) {
    Node *n = node_new(NODE_MEMBER, line);
    n->as.member.obj = obj;
    n->as.member.name = ich_strdup(name);
    return n;
}

Node *node_index(Node *obj, Node *idx, int line) {
    Node *n = node_new(NODE_INDEX, line);
    n->as.index.obj = obj;
    n->as.index.index = idx;
    return n;
}

Node *node_spc_var(const char *name, int line) {
    Node *n = node_new(NODE_SPC_VAR, line);
    n->as.spc_var.name = ich_strdup(name);
    return n;
}

void node_free(Node *n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_STRING: free(n->as.str); break;
        case NODE_IDENT: free(n->as.ident.name); break;
        case NODE_VAR_REF: free(n->as.var_ref.name); break;
        case NODE_BINARY:
            node_free(n->as.binary.l);
            node_free(n->as.binary.r);
            break;
        case NODE_UNARY:
            node_free(n->as.unary.operand);
            break;
        case NODE_CALL:
            node_free(n->as.call.callee);
            for (int i = 0; i < n->as.call.argc; i++) {
                node_free(n->as.call.args[i]);
                if (n->as.call.arg_names) free(n->as.call.arg_names[i]);
            }
            free(n->as.call.args);
            free(n->as.call.arg_names);
            break;
        case NODE_MEMBER:
            node_free(n->as.member.obj);
            free(n->as.member.name);
            break;
        case NODE_INDEX:
            node_free(n->as.index.obj);
            node_free(n->as.index.index);
            break;
        case NODE_SPC_VAR: free(n->as.spc_var.name); break;
        default: break;
    }
    free(n);
}

static void block_free(Block *b) {
    for (int i = 0; i < b->count; i++) stmt_free(b->items[i]);
    free(b->items);
    b->items = NULL;
    b->count = b->cap = 0;
}

void stmt_free(Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case STMT_EXPR:
        case STMT_RET:
            node_free(s->as.expr);
            break;
        case STMT_VAR_DECL:
            free(s->as.var_decl.name);
            node_free(s->as.var_decl.init);
            break;
        case STMT_HOLD:
            free(s->as.hold.name);
            node_free(s->as.hold.size);
            break;
        case STMT_DROP:
            free(s->as.drop.name);
            break;
        case STMT_ASSIGN:
            node_free(s->as.assign.target);
            node_free(s->as.assign.value);
            break;
        case STMT_IF:
            node_free(s->as.if_s.cond);
            block_free(&s->as.if_s.then_blk);
            block_free(&s->as.if_s.else_blk);
            break;
        case STMT_WHILE:
            node_free(s->as.while_s.cond);
            block_free(&s->as.while_s.body);
            break;
        case STMT_PICK:
            block_free(&s->as.pick_s.pick_blk);
            block_free(&s->as.pick_s.error_blk);
            break;
        case STMT_JOIN:
            free(s->as.join.module);
            free(s->as.join.alias);
            break;
        case STMT_SPC_VER:
            break;
        case STMT_FUNC:
            free(s->as.func.name);
            for (int i = 0; i < s->as.func.paramc; i++) free(s->as.func.params[i]);
            free(s->as.func.params);
            block_free(&s->as.func.body);
            break;
    }
    free(s);
}

void program_free(Program *p) {
    if (!p) return;
    block_free(&p->stmts);
}
