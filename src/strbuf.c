#include "strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRBUF_GROWTH_FACTOR 2

void strbuf_init_static(struct strbuf* sb, char* buffer, size_t size) {
    sb->buf = buffer;
    sb->len = 0;
    sb->cap = size;
    sb->dynamic = 0;
    if (size > 0) sb->buf[0] = '\0';
}

void strbuf_init_dynamic(struct strbuf* sb, size_t initial_size) {
    sb->buf = malloc(initial_size);
    sb->len = 0;
    sb->cap = initial_size;
    sb->dynamic = 1;
    sb->buf[0] = '\0';
}

static int strbuf_reserve(struct strbuf* sb, size_t add) {
    size_t need;

    if (!sb->dynamic) return (sb->len + add + 1 <= sb->cap) ? 0 : -1;

    need = sb->len + add + 1;
    if (need <= sb->cap) return 0;

    size_t new_cap = sb->cap * STRBUF_GROWTH_FACTOR;
    if (new_cap < need) new_cap = need;

    char* new_buf = realloc(sb->buf, new_cap);
    if (!new_buf) return -1;

    sb->buf = new_buf;
    sb->cap = new_cap;
    return 0;
}

void strbuf_append_n(struct strbuf* sb, const char* s, size_t n) {
    if (!sb->dynamic) goto copy;
    if (!strbuf_reserve(sb, n)) {
        return;
    }
copy:
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

void strbuf_append(struct strbuf* sb, const char* s) {
    strbuf_append_n(sb, s, strlen(s));
}

void strbuf_appendf(struct strbuf* sb, const char* fmt, ...) {
    va_list ap;
    size_t n;
    size_t available = sb->cap > sb->len ? sb->cap - sb->len : 0;

    va_start(ap, fmt);
    n = vsnprintf(sb->buf + sb->len, available, fmt, ap);
    va_end(ap);

    if (n < available) {
        sb->len += n;
        return;
    }

    if (!sb->dynamic) {
        return;
    }

    if (!strbuf_reserve(sb, n)) {
        return;
    }

    va_start(ap, fmt);
    n = vsnprintf(sb->buf + sb->len, available, fmt, ap);
    va_end(ap);

    sb->len += n;
}

void strbuf_reset(struct strbuf* sb) {
    sb->len = 0;
    sb->buf[0] = '\0';
}
