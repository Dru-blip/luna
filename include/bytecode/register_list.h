#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "value.h"

struct register_list {
    struct lu_value* base;
    size_t capacity;
    size_t top;
};

static inline void register_list_init(struct register_list* a, size_t capacity) {
    a->base = malloc(sizeof(struct lu_value) * capacity);
    a->capacity = capacity;
    a->top = 0;
}
static inline struct lu_value* register_list_alloc_registers(struct register_list* a,
                                                             size_t count) {
    size_t old_top = a->top;
    size_t new_top = old_top + count;

    if (new_top > a->capacity)
        return nullptr;

    a->top = new_top;
    return &a->base[old_top];
}

static inline void register_list_free_registers(struct register_list* a, size_t count) {
    a->top -= count;
}

static inline void register_list_reset(struct register_list* a) {
    a->top = 0;
}

static inline void register_list_destroy(struct register_list* a) {
    free(a->base);
}
