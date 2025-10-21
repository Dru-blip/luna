#pragma once

#include <stddef.h>

struct strbuf {
    char* buf;
    size_t len;
    size_t cap;
    int dynamic;
};

void strbuf_init_static(struct strbuf* sb, char* buffer, size_t size);
void strbuf_init_dynamic(struct strbuf* sb, size_t initial_size);

void strbuf_append(struct strbuf* sb, const char* s);
void strbuf_append_n(struct strbuf* sb, const char* s, size_t n);
void strbuf_appendf(struct strbuf* sb, const char* fmt, ...);

void strbuf_free(struct strbuf* sb);
void strbuf_reset(struct strbuf* sb);
