#pragma once

#include <stdint.h>

#include "arena.h"
#include "tokenizer.h"

typedef struct ast_node ast_node_t;

typedef enum ast_node_kind {
    ast_node_kind_int,
    ast_node_kind_bool,
    ast_node_kind_unop,
    ast_node_kind_binop,
    ast_node_kind_assign,
    ast_node_kind_identifier,

    ast_node_kind_param,

    ast_node_kind_expr_stmt,
    ast_node_kind_return,
    ast_node_kind_block,
    ast_node_kind_if_stmt,
    ast_node_kind_break_stmt,
    ast_node_kind_continue_stmt,
    ast_node_kind_loop_stmt,
    ast_node_kind_while_stmt,
    ast_node_kind_for_stmt,
    ast_node_kind_fn_decl,
} ast_node_kind_t;

typedef struct ast_pair {
    ast_node_t* fst;
    ast_node_t* snd;
} ast_pair_t;

typedef struct ast_unop {
    uint8_t op;
    bool is_prefix;
    ast_node_t* argument;
} ast_unop_t;

typedef struct ast_binop {
    uint8_t op;
    ast_node_t* lhs;
    ast_node_t* rhs;
} ast_binop_t;

typedef struct ast_if_stmt {
    ast_node_t* test;
    ast_node_t* consequent;
    ast_node_t* alternate;
} ast_if_stmt_t;

typedef struct ast_for_stmt {
    ast_node_t* init;
    ast_node_t* test;
    ast_node_t* update;
    ast_node_t* body;
} ast_for_stmt_t;

typedef struct ast_fn_decl {
    span_t name_span;
    ast_node_t** params;
    ast_node_t* body;
} ast_fn_decl_t;

typedef union ast_node_data {
    int64_t int_val;
    char* id;
    ast_node_t* node;
    ast_pair_t pair;
    ast_unop_t unop;
    ast_binop_t binop;
    ast_if_stmt_t if_stmt;
    ast_node_t** list;
    ast_for_stmt_t for_stmt;
    ast_fn_decl_t fn_decl;
} ast_node_data_t;

struct ast_node {
    span_t span;
    ast_node_kind_t kind;
    ast_node_data_t data;
};

typedef struct ast_program {
    const char* source;
    const char* filepath;
    ast_node_t** nodes;
    token_t* tokens;
    arena_t allocator;
} ast_program_t;

void dump_ast(const ast_program_t* program);
void dump_nodes(const ast_node_t** nodes, uint32_t indent);
void dump_node(const ast_node_t* node, uint32_t indent);
