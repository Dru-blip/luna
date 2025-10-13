#include "runtime/heap.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "runtime/istate.h"
#include "runtime/object.h"
#include "stb_ds.h"

static lu_object_t* cell_get(heap_block_t* block, size_t index) {
    return (lu_object_t*)&block->data[index * block->cell_size];
}

static void block_deallocate_cell(heap_block_t* block, lu_object_t* obj) {
    obj->state = obj_state_dead;
    obj->next = block->free_list;
    block->free_list = obj;
    block->allocated_cells--;
}

static lu_object_t* block_allocate_object(heap_block_t* block) {
    if (!block->free_list) return nullptr;

    lu_object_t* obj = block->free_list;
    block->free_list = block->free_list->next;

    block->allocated_cells++;
#ifdef DEBUG

#endif
    obj->state = obj_state_alive;
    obj->is_marked = false;
    return obj;
}

static heap_block_t* block_create(size_t cellsize) {
    heap_block_t* block = malloc(16 * KB);
    if (!block) return nullptr;
    block->cell_size = cellsize;
    block->ncells = ((16 * KB) - sizeof(heap_block_t)) / cellsize;

    for (uint32_t i = 0; i < block->ncells; i++) {
        lu_object_t* new_entry = cell_get(block, i);
        new_entry->state = obj_state_dead;
        if (i == block->ncells - 1) {
            new_entry->next = nullptr;
        } else {
            new_entry->next = cell_get(block, i + 1);
        }
    }

    block->free_list = cell_get(block, 0);
    return block;
}

lu_object_t* heap_allocate_object(heap_t* heap, size_t size) {
    if (heap->bytes_allocated_since_last_gc + size > GC_BYTES_THRESHOLD) {
        // TODO:
        //  collect_garbage(heap);
        heap->bytes_allocated_since_last_gc = 0;
    }
    const uint32_t nblocks = heap->nblocks;
    for (uint32_t i = 0; i < nblocks; i++) {
        heap_block_t* block = heap->blocks[i];
        if (size > block->cell_size) continue;
        lu_object_t* obj = block_allocate_object(block);
        if (obj) {
            heap->bytes_allocated_since_last_gc += size;
            return obj;
        }
    }

    heap_block_t* block = block_create(size);
    arrput(heap->blocks, block);
    heap->nblocks++;
    lu_object_t* obj = block_allocate_object(block);
    heap->bytes_allocated_since_last_gc += size;
    return obj;
}

heap_t* heap_create(lu_istate_t* state) {
    heap_t* heap = calloc(1, sizeof(heap_t));
    if (!heap) return nullptr;
    heap->nblocks = 0;
    heap->blocks = nullptr;
    heap->istate = state;
    heap->bytes_allocated_since_last_gc = 0;
    return heap;
}

static void block_destroy(heap_block_t* block) {
    for (uint32_t i = 0; i < block->ncells; i++) {
        lu_object_t* obj = cell_get(block, i);
        if (obj->state == obj_state_alive && obj->type) {
            obj->type->finialize(obj);
        }
    }
    free(block);
}

void heap_destroy(heap_t* heap) {
    for (uint32_t i = 0; i < heap->nblocks; i++) {
        heap_block_t* block = heap->blocks[i];
        block_destroy(block);
    }
    arrfree(heap->blocks);
    free(heap);
}
