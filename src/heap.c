#include "heap.h"

#include <stdlib.h>

#include "value.h"

#define KB 1024

struct heap* heap_create(struct lu_istate* state) {
    struct heap* heap = malloc(sizeof(struct heap));
    heap->block_list = nullptr;
    heap->block_count = 0;
    heap->istate = state;
    return heap;
}

static struct lu_object* cell_get(struct heap_block* block, size_t index) {
    return (struct lu_object*)&block->data[index * block->cell_size];
}

static struct lu_object* block_allocate_object(struct heap_block* block) {
    struct lu_object* obj = block->free_list;
    block->free_list = block->free_list->next;
    return obj;
}

static struct heap_block* block_create(size_t cell_size) {
    struct heap_block* block = malloc(16 * KB);
    if (!block) return nullptr;
    block->cell_size = cell_size;
    block->cell_count = ((16 * KB) - sizeof(struct heap_block)) / cell_size;

    for (uint32_t i = 0; i < block->cell_count; i++) {
        struct lu_object* new_entry = cell_get(block, i);
        new_entry->state = OBJECT_STATE_DEAD;
        if (i == block->cell_count - 1) {
            new_entry->next = nullptr;
        } else {
            new_entry->next = cell_get(block, i + 1);
        }
    }

    block->free_list = cell_get(block, 0);
    return block;
}

struct lu_object* heap_allocate_object(struct heap* heap, size_t size) {
    struct heap_block* block = heap->block_list;
    while (block) {
        if (size < block->cell_size) {
            struct lu_object* obj = block_allocate_object(block);
            if (obj) {
                heap->bytes_allocated_since_last_gc += size;
                return obj;
            }
        }
        block = block->next;
    }

    block = block_create(size);
    block->next = heap->block_list;
    heap->block_list = block;
    struct lu_object* obj = block_allocate_object(block);
    heap->bytes_allocated_since_last_gc += size;
    return obj;
}

void collect_garbage(struct heap* heap) {
    // TODO: implement collection
}

void heap_destroy(struct heap* heap) {
    struct heap_block* block = heap->block_list;
    while (block) {
        struct heap_block* next = block->next;
        free(block);
        block = next;
    }
    free(heap);
}
