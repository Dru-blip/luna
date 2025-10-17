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

typedef struct lu_gc_objectset {
    lu_object_t** entries;
    size_t capacity;
    size_t size;
    double load_factor;
} lu_gc_objectset_t;

typedef struct lu_gc_objectset_iter {
    lu_gc_objectset_t* set;
    size_t index;
} lu_gc_objectset_iter_t;

static inline lu_gc_objectset_iter_t lu_gc_objectset_iter_new(
    lu_gc_objectset_t* set) {
    lu_gc_objectset_iter_t iter = {set, 0};
    return iter;
}

static inline lu_object_t* lu_gc_objectset_iter_next(
    lu_gc_objectset_iter_t* iter) {
    while (iter->index < iter->set->capacity) {
        lu_object_t* key = iter->set->entries[iter->index++];
        if (key) return key;
    }
    return nullptr;
}

lu_gc_objectset_t* lu_gc_objectset_new(size_t initial_capacity);
bool lu_gc_objectset_insert(lu_gc_objectset_t* set, void* key);
bool lu_gc_objectset_contains(lu_gc_objectset_t* set, void* key);
void lu_gc_objectset_clear(lu_gc_objectset_t* set);
void lu_gc_objectset_free(lu_gc_objectset_t* set);

heap_t* heap_create(struct lu_istate* istate);
lu_object_t* heap_allocate_object(heap_t* heap, size_t size);
void collect_garbage(heap_t* heap);
void heap_destroy(heap_t* heap);
