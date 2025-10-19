#pragma once

#include <stddef.h>
#include <stdint.h>

struct heap_block {
    size_t cell_size;
    size_t cell_count;
    struct lu_object* free_list;
    struct heap_block* next;
    uint8_t data[];
};

struct heap {
    size_t block_count;
    struct heap_block* block_list;
    size_t bytes_allocated_since_last_gc;
    struct lu_istate* istate;
};

struct heap* heap_create(struct lu_istate* state);
struct lu_object* heap_allocate_object(struct heap* heap, size_t size);
void collect_garbage(struct heap* heap);
void heap_destroy(struct heap* heap);
