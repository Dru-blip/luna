#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define KB 1024
#define ARENA_BLOCK_DEFAULT_CAPACITY (4 * KB)

struct arena_block {
    struct arena_block* next;
    size_t capacity;
    size_t used;
    uint8_t data[];
};

struct arena {
    struct arena_block* head;
};

struct arena_block* arena_new_block(size_t capacity);
void arena_init(struct arena* arena);
void* arena_alloc(struct arena* arena, size_t size);
void arena_reset(struct arena* arena);
void arena_destroy(struct arena* arena);

#ifdef ARENA_IMPLEMENTATION

static inline void* arena_block_allocate(struct arena_block* block,
                                         size_t size) {
    void* obj = block->data + block->used;
    block->used += size;
    return obj;
}

struct arena_block* arena_new_block(size_t capacity) {
    struct arena_block* blk =
        (struct arena_block*)malloc(sizeof(struct arena_block) + capacity);
    blk->capacity = capacity;
    blk->used = 0;
    blk->next = nullptr;
    return blk;
}

void arena_init(struct arena* arena) { arena->head = nullptr; }

void* arena_alloc(struct arena* arena, size_t size) {
    struct arena_block* blk = arena->head;
    while (blk) {
        if (blk->used + size < blk->capacity) {
            return arena_block_allocate(blk, size);
        }
        blk = blk->next;
    }

    size_t blk_capacity = ARENA_BLOCK_DEFAULT_CAPACITY > size
                              ? ARENA_BLOCK_DEFAULT_CAPACITY
                              : size;
    blk = arena_new_block(blk_capacity);
    blk->next = arena->head;
    arena->head = blk;

    return arena_block_allocate(blk, size);
}

void arena_reset(struct arena* arena) {
    for (struct arena_block* blk = arena->head; blk; blk = blk->next) {
        blk->used = 0;
    }
}

void arena_destroy(struct arena* arena) {
    struct arena_block* blk = arena->head;
    while (blk) {
        struct arena_block* next = blk->next;
        free(blk);
        blk = next;
    }
    arena->head = nullptr;
}

#endif
