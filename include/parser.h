#pragma once

#include <stdint.h>

#include "arena.h"
#include "ast.h"
#include "tokenizer.h"

enum operator_kind {
    OPERATOR_INFIX,
    OPERATOR_ASSIGN,
    OPERATOR_POSTFIX,
};

struct operator{
    enum operator_kind kind;
    enum token_kind token_kind;
    int8_t lbp;
    int8_t rbp;
    uint8_t op;
    enum ast_node_kind node_kind;
};

struct parser {
    const char* source;
    const char* filename;
    uint32_t pos;
    struct token* cur_token;
    struct token* token_list;
    struct arena allocator;
};

struct ast_program parse_program(const char* filename, const char* source);
