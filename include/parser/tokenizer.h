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
    token_kind_plus_equal,
    token_kind_minus,
    token_kind_minus_equal,
    token_kind_asterisk,
    token_kind_asterisk_equal,
    token_kind_slash,
    token_kind_slash_equal,
    token_kind_modulus,
    token_kind_modulus_equal,

    token_kind_less,
    token_kind_greater,
    token_kind_less_equal,
    token_kind_greater_equal,

    token_kind_less_less,
    token_kind_greater_greater,

    token_kind_equal_equal,
    token_kind_equal,

    token_kind_bang,
    token_kind_bang_equal,

    token_kind_ampersand,
    token_kind_ampersand_ampersand,

    token_kind_pipe,
    token_kind_pipe_pipe,

    token_kind_integer,
    token_kind_identifier,

    token_kind_keyword_return,

    token_kind_eof,
} token_kind_t;

static const char* token_kind_labels[] = {
    "+",  // token_kind_plus
    "-",  // token_kind_minus
    "*",  // token_kind_asterisk,
    "/",  // token_kind_slash
    "%",  // token_kind_modulus,

    "integer",     // token_kind_integer
    "identifier",  // token_kind_identifier

    "return",  // token_kind_keyword_return

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

typedef struct keyword {
    const char* key;
    token_kind_t value;
} keyword_t;

static keyword_t keywords[] = {
    {"return", token_kind_keyword_return},
};

token_t* tokenize(const char* source);
