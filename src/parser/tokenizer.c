#include "parser/tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_ds.h"

static token_kind_t lookup_keyword(const char* ident) {
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (strcmp(keywords[i].key, ident) == 0) {
            return keywords[i].value;
        }
    }
    return token_kind_identifier;
}

static char current(tokenizer_t* t) {
    return (t->pos < t->len) ? t->src[t->pos] : '\0';
}

static void advance(tokenizer_t* t) {
    t->col++;
    t->pos++;
}

static void skip_whitespace(tokenizer_t* t) {
    while (true) {
        char c = current(t);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(t);
        } else if (c == '\n') {
            t->col = 1;
            t->line++;
            t->pos++;
            t->line_start = t->pos;
        } else if (c == '/') {
            if (t->pos + 1 < t->len) {
                char next = t->src[t->pos + 1];
                if (next == '/') {
                    t->pos += 2;
                    t->col += 2;
                    while (true) {
                        c = current(t);
                        if (c == '\n' || c == '\0') {
                            break;
                        }
                        advance(t);
                    }
                    continue;
                }
                if (next == '*') {
                    t->pos += 2;
                    t->col += 2;
                    while (true) {
                        c = current(t);
                        if (c == '\0') {
                            printf("Unterminated block comment at %d:%d",
                                   t->line, t->col);
                            exit(1);
                        }
                        if (c == '\n') {
                            t->col = 1;
                            t->line++;
                            t->pos++;
                            t->line_start = t->pos;
                            continue;
                        }
                        if (c == '*' && t->pos + 1 < t->len &&
                            t->src[t->pos + 1] == '/') {
                            advance(t);
                            advance(t);
                            break;
                        }
                        advance(t);
                    }
                    continue;
                }
                break;
            }
            break;
        } else {
            break;
        }
    }
}

static token_t next_token(tokenizer_t* t) {
    skip_whitespace(t);

    span_t start = {t->line, t->col, (uint32_t)t->pos, t->pos + 1};
    token_data_t data = {};
    char c = current(t);
    token_kind_t kind;

    switch (c) {
        case '\0':
            advance(t);
            kind = token_kind_eof;
            break;
        case '+': {
            advance(t);
            kind = token_kind_plus;
            break;
        }
        case '-': {
            advance(t);
            kind = token_kind_minus;
            break;
        }
        case '*': {
            advance(t);
            kind = token_kind_asterisk;
            break;
        }
        case '/': {
            advance(t);
            kind = token_kind_slash;
            break;
        }
        case '%': {
            advance(t);
            kind = token_kind_modulus;
            break;
        }
        default:
            if (isdigit(c)) {
                while (isdigit(current(t))) advance(t);
                char* endptr;
                char* nptr = strdup(t->src + start.start);
                data.int_val = strtol(nptr, &endptr, 10);
                kind = token_kind_integer;
                free(nptr);
            } else if (isalpha(c) || c == '_') {
                size_t start_pos = t->pos;
                while (isalnum(current(t)) || current(t) == '_') advance(t);
                size_t len = t->pos - start_pos;
                char* ident = strndup(t->src + start_pos, len);
                kind = lookup_keyword(ident);
                free(ident);
            } else {
                fprintf(stderr, "unexpected character '%c'\n", c);
                exit(1);
            }
            break;
    }

    start.end = t->pos;
    return (token_t){
        .kind = kind,
        .span = start,
        .data = data,
    };
}

token_t* tokenize(const char* source) {
    tokenizer_t tokenizer = {
        .src = source,
        .len = strlen(source),
        .pos = 0,
        .line = 1,
        .col = 1,
        .line_start = 0,
    };

    token_t* tokens = NULL;

    while (tokenizer.pos <= tokenizer.len) {
        const token_t tok = next_token(&tokenizer);
        arrput(tokens, tok);
        if (tok.kind == token_kind_eof) break;
    }

    return tokens;
}
