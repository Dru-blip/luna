#pragma once

#include <stddef.h>
#include <stdint.h>

#include "runtime/object.h"

#define KB 1024
#define GC_BYTES_THRESHOLD KB * KB * 4

typedef struct heap_block {
    size_t cell_size;
    size_t ncells;
    size_t allocated_cells;
    lu_object_t* free_list;
    uint8_t data[];
} heap_block_t;

typedef struct heap {
    size_t nblocks;
    heap_block_t** blocks;
    size_t bytes_allocated_since_last_gc;
    struct lu_istate* istate;
} heap_t;

typedef struct roots {
    lu_object_t* key;
    void* value;
} gc_roots_t;

heap_t* heap_create(struct lu_istate* istate);
lu_object_t* heap_allocate_object(heap_t* heap, size_t size);
void collect_garbage(heap_t* heap);
void heap_destroy(heap_t* heap);
