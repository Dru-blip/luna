#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "arena.h"
#include "tokenizer.h"

struct ast_node;

enum ast_node_kind {
    AST_NODE_INT,
    AST_NODE_BOOL,
    AST_NODE_STR,
    AST_NODE_UNOP,
    AST_NODE_BINOP,
    AST_NODE_ASSIGN,
    AST_NODE_IDENTIFIER,

    AST_NODE_PARAM,
    AST_NODE_CALL,

    AST_NODE_MEMBER_EXPR,

    AST_NODE_ARRAY_EXPR,

    AST_NODE_EXPR_STMT,
    AST_NODE_RETURN,
    AST_NODE_BLOCK,
    AST_NODE_IF_STMT,
    AST_NODE_BREAK_STMT,
    AST_NODE_CONTINUE_STMT,
    AST_NODE_LOOP_STMT,
    AST_NODE_WHILE_STMT,
    AST_NODE_FOR_STMT,
    AST_NODE_FN_DECL,
};

struct ast_pair {
    struct ast_node* fst;
    struct ast_node* snd;
};

struct ast_unop {
    uint8_t op;
    bool is_prefix;
    struct ast_node* argument;
};

struct ast_binop {
    uint8_t op;
    struct ast_node* lhs;
    struct ast_node* rhs;
};

struct ast_call {
    uint8_t argc;
    struct ast_node* callee;
    struct ast_node** args;
};

struct ast_member_expr {
    bool is_computed;
    struct ast_node* object;
    struct span property_name;
};

struct ast_if_stmt {
    struct ast_node* test;
    struct ast_node* consequent;
    struct ast_node* alternate;
};

struct ast_for_stmt {
    struct ast_node* init;
    struct ast_node* test;
    struct ast_node* update;
    struct ast_node* body;
};

struct ast_fn_decl {
    struct span name_span;
    struct ast_node** params;
    struct ast_node* body;
};

union ast_node_data {
    int64_t int_val;
    char* id;
    struct ast_node* node;
    struct ast_pair pair;
    struct ast_unop unop;
    struct ast_binop binop;
    struct ast_call call;
    struct ast_member_expr member_expr;
    struct ast_if_stmt if_stmt;
    struct ast_node** list;
    struct ast_for_stmt for_stmt;
    struct ast_fn_decl fn_decl;
};

struct ast_node {
    struct span span;
    enum ast_node_kind kind;
    union ast_node_data data;
};

struct ast_program {
    const char* source;
    const char* filepath;
    struct ast_node** nodes;
    struct token* tokens;
    struct arena allocator;
};

void dump_ast(const struct ast_program* program);
void dump_nodes(struct ast_node** nodes, uint32_t indent);
void dump_node(const struct ast_node* node, uint32_t indent);
