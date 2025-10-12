#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct span {
    uint32_t line;
    uint32_t col;
    uint32_t start;
    uint32_t end;
} span_t;

typedef enum token_kind {
    token_kind_plus,
    token_kind_minus,
    token_kind_asterisk,
    token_kind_slash,
    token_kind_modulus,

    token_kind_integer,

    token_kind_eof,
} token_kind_t;

static const char* token_kind_labels[] = {
    "+",  // token_kind_plus
    "-",  // token_kind_minus
    "*",  // token_kind_asterisk,
    "/",  // token_kind_slash
    "%",  // token_kind_modulus,

    "integer",  // token_kind_integer

    "eof",  // token_kind_eof
};

typedef union token_data {
    int64_t int_val;
    char* str_val;
} token_data_t;

typedef struct token {
    token_kind_t kind;
    span_t span;
    token_data_t data;
} token_t;

typedef struct tokenizer {
    const char* src;
    size_t len;
    size_t pos;
    uint32_t line;
    uint32_t col;
    uint32_t line_start;
} tokenizer_t;

token_t* tokenize(const char* source);
