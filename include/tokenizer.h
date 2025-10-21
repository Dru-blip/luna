#pragma once

#include <stddef.h>
#include <stdint.h>

struct span {
    uint32_t line;
    uint32_t col;
    uint32_t start;
    uint32_t end;
};

#define SPAN_MERGE(a, b) \
    ((struct span){      \
        .line = (a).line, .col = (a).col, .start = (a).start, .end = (b).end})

enum token_kind {
    TOKEN_PLUS,
    TOKEN_PLUS_EQUAL,
    TOKEN_MINUS,
    TOKEN_MINUS_EQUAL,
    TOKEN_ASTERISK,
    TOKEN_ASTERISK_EQUAL,
    TOKEN_SLASH,
    TOKEN_SLASH_EQUAL,
    TOKEN_MODULUS,
    TOKEN_MODULUS_EQUAL,

    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER_EQUAL,

    TOKEN_LESS_LESS,
    TOKEN_GREATER_GREATER,

    TOKEN_EQUAL_EQUAL,
    TOKEN_EQUAL,

    TOKEN_BANG,
    TOKEN_BANG_EQUAL,

    TOKEN_AMPERSAND,
    TOKEN_AMPERSAND_AMPERSAND,

    TOKEN_PIPE,
    TOKEN_PIPE_PIPE,

    TOKEN_INTEGER,
    TOKEN_IDENTIFIER,
    TOKEN_STRING,

    TOKEN_SEMICOLON,
    TOKEN_LPAREN,
    TOKEN_RPAREN,

    TOKEN_LBRACE,
    TOKEN_RBRACE,

    TOKEN_LBRACKET,
    TOKEN_RBRACKET,

    TOKEN_COMMA,
    TOKEN_DOT,

    TOKEN_KEYWORD_RETURN,
    TOKEN_KEYWORD_TRUE,
    TOKEN_KEYWORD_FALSE,
    TOKEN_KEYWORD_IF,
    TOKEN_KEYWORD_ELSE,

    TOKEN_KEYWORD_LOOP,
    TOKEN_KEYWORD_WHILE,
    TOKEN_KEYWORD_FOR,

    TOKEN_KEYWORD_BREAK,
    TOKEN_KEYWORD_CONTINUE,

    TOKEN_KEYWORD_FN,

    TOKEN_EOF,
};

static const char* token_labels[] = {
    "+",   // TOKEN_PLUS
    "+=",  // TOKEN_PLUS_EQUAL
    "-",   // TOKEN_MINUS
    "-=",  // TOKEN_MINUS_EQUAL
    "*",   // TOKEN_ASTERISK
    "*=",  // TOKEN_ASTERISK_EQUAL
    "/",   // TOKEN_SLASH
    "/=",  // TOKEN_SLASH_EQUAL
    "%",   // TOKEN_MODULUS
    "%=",  // TOKEN_MODULUS_EQUAL

    "<",   // TOKEN_LESS
    ">",   // TOKEN_GREATER
    "<=",  // TOKEN_LESS_EQUAL
    ">=",  // TOKEN_GREATER_EQUAL

    "<<",  // TOKEN_LESS_LESS
    ">>",  // TOKEN_GREATER_GREATER

    "==",  // TOKEN_EQUAL_EQUAL
    "=",   // TOKEN_EQUAL

    "!",   // TOKEN_BANG
    "!=",  // TOKEN_BANG_EQUAL

    "&",   // TOKEN_AMPERSAND
    "&&",  // TOKEN_AMPERSAND_AMPERSAND

    "|",   // TOKEN_PIPE
    "||",  // TOKEN_PIPE_PIPE

    "integer",     // TOKEN_INTEGER
    "identifier",  // TOKEN_IDENTIFIER
    "string",      // TOKEN_STRING

    ";",  // TOKEN_SEMICOLON
    "(",  // TOKEN_LPAREN
    ")",  // TOKEN_RPAREN
    "{",  // TOKEN_LBRACE
    "}",  // TOKEN_RBRACE
    "[",  // TOKEN_LBRACKET
    "]",  // TOKEN_RBRACKET
    ",",  // TOKEN_COMMA
    ".",  // TOKEN_DOT

    "return",    // TOKEN_KEYWORD_RETURN
    "true",      // TOKEN_KEYWORD_TRUE
    "false",     // TOKEN_KEYWORD_FALSE
    "if",        // TOKEN_KEYWORD_IF
    "else",      // TOKEN_KEYWORD_ELSE
    "loop",      // TOKEN_KEYWORD_LOOP
    "while",     // TOKEN_KEYWORD_WHILE
    "for",       // TOKEN_KEYWORD_FOR
    "break",     // TOKEN_KEYWORD_BREAK
    "continue",  // TOKEN_KEYWORD_CONTINUE
    "fn",        // TOKEN_KEYWORD_FN

    "eof",  // TOKEN_EOF
};

union token_data {
    int64_t int_val;
    char* str_val;
};

struct token {
    enum token_kind kind;
    struct span span;
    union token_data data;
};

struct tokenizer {
    const char* src;
    size_t len;
    size_t pos;
    uint32_t line;
    uint32_t col;
    uint32_t line_start;
};

struct keyword {
    const char* key;
    enum token_kind value;
};

static struct keyword keywords[] = {
    {"return", TOKEN_KEYWORD_RETURN}, {"true", TOKEN_KEYWORD_TRUE},
    {"false", TOKEN_KEYWORD_FALSE},   {"if", TOKEN_KEYWORD_IF},
    {"else", TOKEN_KEYWORD_ELSE},     {"loop", TOKEN_KEYWORD_LOOP},
    {"while", TOKEN_KEYWORD_WHILE},   {"for", TOKEN_KEYWORD_FOR},
    {"break", TOKEN_KEYWORD_BREAK},   {"continue", TOKEN_KEYWORD_CONTINUE},
    {"fn", TOKEN_KEYWORD_FN},
};

struct token* tokenize(const char* source);
