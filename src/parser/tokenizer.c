#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_ds.h"

static enum token_kind lookup_keyword(const char* ident) {
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (strcmp(keywords[i].key, ident) == 0) {
            return keywords[i].value;
        }
    }
    return TOKEN_IDENTIFIER;
}

static char current(struct tokenizer* t) {
    return (t->pos < t->len) ? t->src[t->pos] : '\0';
}

static void advance(struct tokenizer* t) {
    t->col++;
    t->pos++;
}

static void skip_whitespace(struct tokenizer* t) {
    while (1) {
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
                    while (1) {
                        c = current(t);
                        if (c == '\n' || c == '\0') break;
                        advance(t);
                    }
                    continue;
                }
                if (next == '*') {
                    t->pos += 2;
                    t->col += 2;
                    while (1) {
                        c = current(t);
                        if (c == '\0') {
                            fprintf(stderr,
                                    "Unterminated block comment at %d:%d\n",
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
            }
            break;
        } else {
            break;
        }
    }
}

static struct token next_token(struct tokenizer* t) {
    skip_whitespace(t);

    struct span start = {t->line, t->col, (uint32_t)t->pos, t->pos + 1};
    union token_data data = {};
    char c = current(t);
    enum token_kind kind;

    switch (c) {
        case '\0':
            advance(t);
            kind = TOKEN_EOF;
            break;
        case '(':
            advance(t);
            kind = TOKEN_LPAREN;
            break;
        case ')':
            advance(t);
            kind = TOKEN_RPAREN;
            break;
        case '{':
            advance(t);
            kind = TOKEN_LBRACE;
            break;
        case '}':
            advance(t);
            kind = TOKEN_RBRACE;
            break;
        case '[':
            advance(t);
            kind = TOKEN_LBRACKET;
            break;
        case ']':
            advance(t);
            kind = TOKEN_RBRACKET;
            break;
        case ';':
            advance(t);
            kind = TOKEN_SEMICOLON;
            break;
        case '+':
            advance(t);
            kind = (current(t) == '=') ? (advance(t), TOKEN_PLUS_EQUAL)
                                       : TOKEN_PLUS;
            break;
        case '-':
            advance(t);
            kind = (current(t) == '=') ? (advance(t), TOKEN_MINUS_EQUAL)
                                       : TOKEN_MINUS;
            break;
        case '*':
            advance(t);
            kind = (current(t) == '=') ? (advance(t), TOKEN_ASTERISK_EQUAL)
                                       : TOKEN_ASTERISK;
            break;
        case '/':
            advance(t);
            kind = (current(t) == '=') ? (advance(t), TOKEN_SLASH_EQUAL)
                                       : TOKEN_SLASH;
            break;
        case '%':
            advance(t);
            kind = (current(t) == '=') ? (advance(t), TOKEN_MODULUS_EQUAL)
                                       : TOKEN_MODULUS;
            break;
        case '=':
            advance(t);
            kind = (current(t) == '=') ? (advance(t), TOKEN_EQUAL_EQUAL)
                                       : TOKEN_EQUAL;
            break;
        case '!':
            advance(t);
            kind = (current(t) == '=') ? (advance(t), TOKEN_BANG_EQUAL)
                                       : TOKEN_BANG;
            break;
        case '<':
            advance(t);
            if (current(t) == '=') {
                advance(t);
                kind = TOKEN_LESS_EQUAL;
            } else if (current(t) == '<') {
                advance(t);
                kind = TOKEN_LESS_LESS;
            } else
                kind = TOKEN_LESS;
            break;
        case '>':
            advance(t);
            if (current(t) == '=') {
                advance(t);
                kind = TOKEN_GREATER_EQUAL;
            } else if (current(t) == '>') {
                advance(t);
                kind = TOKEN_GREATER_GREATER;
            } else
                kind = TOKEN_GREATER;
            break;
        case '&':
            advance(t);
            kind = (current(t) == '&') ? (advance(t), TOKEN_AMPERSAND_AMPERSAND)
                                       : TOKEN_AMPERSAND;
            break;
        case '|':
            advance(t);
            kind = (current(t) == '|') ? (advance(t), TOKEN_PIPE_PIPE)
                                       : TOKEN_PIPE;
            break;
        case ',':
            advance(t);
            kind = TOKEN_COMMA;
            break;
        case '.':
            advance(t);
            kind = TOKEN_DOT;
            break;
        case ':':
            advance(t);
            kind = TOKEN_COLON;
            break;
        case '"': {
            advance(t);
            const char* start = t->src + t->pos;

            while (true) {
                char c = current(t);
                if (c == '"') {
                    break;
                } else if (c == '\\') {
                    advance(t);
                    if (current(t) == '\0') {
                        fprintf(
                            stderr,
                            "Unterminated escape sequence at line %d, col %d\n",
                            t->line, t->col);
                        exit(1);
                    }
                    advance(t);
                } else if (c == '\0') {
                    fprintf(stderr,
                            "Unterminated string literal starting at line %d, "
                            "col %d\n",
                            t->line, t->col);
                    exit(1);
                } else {
                    advance(t);
                }
            }

            kind = TOKEN_STRING;
            data.str_val = start;

            advance(t);
            break;
        }
        default:
            if (isdigit(c)) {
                size_t start_pos = t->pos;
                while (isdigit(current(t))) advance(t);
                size_t len = t->pos - start_pos;
                char* num_str = strndup(t->src + start_pos, len);
                data.int_val = strtoll(num_str, nullptr, 10);
                free(num_str);
                kind = TOKEN_INTEGER;
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
    return (struct token){.kind = kind, .span = start, .data = data};
}

struct token* tokenize(const char* source) {
    struct tokenizer tokenizer = {
        .src = source,
        .len = strlen(source),
        .pos = 0,
        .line = 1,
        .col = 1,
        .line_start = 0,
    };

    struct token* tokens = nullptr;

    while (tokenizer.pos <= tokenizer.len) {
        struct token tok = next_token(&tokenizer);
        arrput(tokens, tok);
        if (tok.kind == TOKEN_EOF) break;
    }

    return tokens;
}
