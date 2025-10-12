#pragma once

#include <stdint.h>

#include "arena.h"
#include "ast.h"
#include "tokenizer.h"

typedef enum operator_kind {
    operator_kind_infix,
    operator_kind_assign,
    operator_kind_postfix,
} operator_kind_t;

typedef struct operator{
    operator_kind_t kind;
    token_kind_t token_kind;
    int8_t lbp;
    int8_t rbp;
    uint8_t op;
    ast_node_kind_t node_kind;
}operator_t;

typedef struct parser {
    const char* source;
    const char* filename;
    uint32_t pos;
    token_t* cur_token;
    token_t* token_list;
    arena_t allocator;
} parser_t;

ast_program_t parse_program(const char* filename, const char* source);
