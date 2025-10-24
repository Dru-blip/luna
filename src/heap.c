#include "heap.h"

#include <stdio.h>
#include <stdlib.h>

#include "eval.h"
#include "stb_ds.h"
#include "value.h"

#define KB 1024

struct heap* heap_create(struct lu_istate* state) {
    struct heap* heap = malloc(sizeof(struct heap));
    heap->block_list = nullptr;
    heap->block_count = 0;
    heap->bytes_allocated_since_last_gc = 0;
    heap->istate = state;
    return heap;
}

static struct lu_object* cell_get(struct heap_block* block, size_t index) {
    return (struct lu_object*)&block->data[index * block->cell_size];
}

static struct lu_object* block_allocate_object(struct heap_block* block) {
    if (!block->free_list) {
        return nullptr;
    }
    struct lu_object* obj = block->free_list;
    obj->state = OBJECT_STATE_ALIVE;
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
    if (heap->bytes_allocated_since_last_gc + size > GC_BYTES_THRESHOLD) {
        collect_garbage(heap);
        heap->bytes_allocated_since_last_gc = 0;
    }
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

static void collect_roots(struct heap* heap, struct lu_objectset* roots) {
#ifdef DEBUG
    printf("Collecting roots...\n");
#endif
    lu_objectset_add(roots, heap->istate->global_object);
    lu_objectset_add(roots, heap->istate->module_cache);

    if (heap->istate->error) {
        lu_objectset_add(roots, heap->istate->error);
    }

    struct execution_context* ctx = heap->istate->context_stack;
    while (ctx) {
        struct call_frame* frame = ctx->call_stack;
        lu_objectset_add(roots, ctx->global_scope.variables);
        while (frame) {
            lu_objectset_add(roots, ctx->call_stack->module);
            lu_objectset_add(roots, frame->function);
            if (frame->self) {
                lu_objectset_add(roots, frame->self);
            }
            if (lu_is_object(frame->return_value)) {
                lu_objectset_add(roots, lu_as_object(frame->return_value));
            }
            size_t scope_count = arrlen(frame->scopes);
            struct scope* scope;
            for (size_t i = 0; i < scope_count; i++) {
                scope = &frame->scopes[i];
                lu_objectset_add(roots, scope->variables);
            }
            frame = frame->parent;
        }
        ctx = ctx->prev;
    }
    // adding interned strings to roots if any missed
    struct string_map_iter it;
    string_map_iter_init(&it, &heap->istate->string_pool.strings);

    // printf("adding interned strings to gc roots\n");
    struct string_map_entry* entry;
    while ((entry = string_map_iter_next(&it))) {
        // printf("cl: adding string %p\n", entry->value);
        lu_objectset_add(roots, lu_cast(struct lu_object, entry->value));
    }

#ifdef DEBUG
    // printf("Roots collected %ld.\n", hmlen(*roots));
#endif
}

static void collect_live_cells(struct heap* heap, struct lu_objectset* roots,
                               struct lu_objectset* live_cells) {
#ifdef DEBUG
    printf("Collecting live cells...\n");
#endif
    struct lu_objectset_iter iter = lu_objectset_iter_new(roots);
    struct lu_object* obj;
    while ((obj = lu_objectset_iter_next(&iter))) {
        obj->vtable->visit(obj, live_cells);
    }
#ifdef DEBUG
    // printf("Total live cells collected: %ld\n", hmlen(*live_cells));
#endif
}

static void clear_mark_bits(struct heap* heap) {
    for (struct heap_block* block = heap->block_list; block;
         block = block->next) {
        for (size_t j = 0; j < block->cell_count; ++j) {
            struct lu_object* obj = cell_get(block, j);
            obj->is_marked = false;
        }
    }
}

static void mark_live_cells(struct heap* heap,
                            struct lu_objectset* live_cells) {
    struct lu_objectset_iter iter = lu_objectset_iter_new(live_cells);
    struct lu_object* obj;
    while ((obj = lu_objectset_iter_next(&iter))) {
        obj->is_marked = true;
    }
}

static void block_deallocate_cell(struct heap_block* block,
                                  struct lu_object* obj) {
    obj->state = OBJECT_STATE_DEAD;
    obj->next = block->free_list;
    block->free_list = obj;
}

static void sweep_dead_cells(struct heap* heap) {
    for (struct heap_block* block = heap->block_list; block;
         block = block->next) {
        for (size_t j = 0; j < block->cell_count; ++j) {
            struct lu_object* obj = cell_get(block, j);
            if (!obj->is_marked && obj->state == OBJECT_STATE_ALIVE) {
                obj->vtable->finalize(obj);
                block_deallocate_cell(block, obj);
            }
        }
    }
}

void collect_garbage(struct heap* heap) {
    struct lu_objectset* roots = lu_objectset_new(16);
    struct lu_objectset* live_cells = lu_objectset_new(16);
    collect_roots(heap, roots);
    collect_live_cells(heap, roots, live_cells);
    clear_mark_bits(heap);
    mark_live_cells(heap, live_cells);
    sweep_dead_cells(heap);
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
