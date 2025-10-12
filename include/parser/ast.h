#pragma once

#include <stdint.h>

#include "arena.h"
#include "tokenizer.h"

typedef struct ast_node ast_node_t;

typedef enum ast_node_kind {
    ast_node_kind_int,
    ast_node_kind_binop,

    ast_node_kind_return,
} ast_node_kind_t;

typedef struct ast_pair {
    ast_node_t* fst;
    ast_node_t* snd;
} ast_pair_t;

typedef struct ast_binop {
    uint8_t op;
    ast_node_t* lhs;
    ast_node_t* rhs;
} ast_binop_t;

typedef union ast_node_data {
    int64_t int_val;
    ast_node_t* node;
    ast_pair_t pair;
    ast_binop_t binop;
    char* id;
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
