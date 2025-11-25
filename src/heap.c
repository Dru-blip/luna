#include "heap.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "bytecode/interpreter.h"
#include "bytecode/vm.h"
#include "stb_ds.h"
#include "value.h"

// #define GC_DEBUG_STATS

#ifdef GC_DEBUG_STATS
struct heap_stats {
    size_t total_allocs;
    size_t total_frees;
    size_t total_blocks;
    size_t total_cells;
    size_t gc_count;
    size_t last_gc_live;
    size_t last_gc_freed;
    size_t bytes_allocated;
};

static struct heap_stats g_stats = {0};

static inline void stats_print_summary(struct heap* heap) {
    fprintf(stderr,
            "[heap] blocks=%zu  allocs=%zu  frees=%zu  live=%zu  bytes=%zuKB  "
            "gc=%zu\n",
            g_stats.total_blocks, g_stats.total_allocs, g_stats.total_frees,
            g_stats.total_allocs - g_stats.total_frees, g_stats.bytes_allocated / 1024,
            g_stats.gc_count);
}

#endif

struct heap* heap_create(struct lu_istate* state) {
    struct heap* heap = malloc(sizeof(struct heap));
    heap->block_list = nullptr;
    heap->block_count = 0;
    heap->bytes_allocated_since_last_gc = 0;
    heap->istate = state;
#ifdef GC_DEBUG_STATS
    fprintf(stderr, "[heap] created new heap %p\n", (void*)heap);
#endif
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
    if (!block)
        return nullptr;
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

#ifdef GC_DEBUG_STATS
    g_stats.total_blocks++;
    g_stats.total_cells += block->cell_count;
    fprintf(stderr, "[heap] created block %p (%zu cells of %zu bytes)\n", (void*)block,
            block->cell_count, block->cell_size);
#endif
    return block;
}

struct lu_object* heap_allocate_object(struct heap* heap, size_t size) {
    if (heap->bytes_allocated_since_last_gc + size > GC_BYTES_THRESHOLD) {
#ifdef GC_DEBUG_STATS

        fprintf(stderr, "[heap] GC threshold reached (%zu bytes), collecting...\n",
                heap->bytes_allocated_since_last_gc);
#endif
        collect_garbage(heap);
        heap->bytes_allocated_since_last_gc = 0;
    }
    struct heap_block* block = heap->block_list;
    while (block) {
        if (size <= block->cell_size) {
            struct lu_object* obj = block_allocate_object(block);
            if (obj) {
#ifdef GC_DEBUG_STATS
                g_stats.total_allocs++;
                g_stats.bytes_allocated += size;
#endif
                heap->bytes_allocated_since_last_gc += size;
                return obj;
            }
        }
        block = block->next;
    }

    block = block_create(size);
    if (!block) {
        printf("Failed to create block\n");
        return nullptr;
    }
    block->next = heap->block_list;
    heap->block_list = block;
    struct lu_object* obj = block_allocate_object(block);

#ifdef GC_DEBUG_STATS
    g_stats.total_allocs++;
    g_stats.bytes_allocated += size;
    if (g_stats.total_allocs % 1000000 == 0)
        stats_print_summary(heap);
#endif
    heap->bytes_allocated_since_last_gc += size;
    return obj;
}

static void collect_roots(struct heap* heap, struct lu_objectset* roots) {
#ifdef DEBUG
    printf("Collecting roots...\n");
#endif
    // lu_objectset_add(roots, heap->istate->global_object);
    lu_objectset_add(roots, heap->istate->vm->global_object);
    lu_objectset_add(roots, heap->istate->module_cache);
    lu_objectset_add(roots, heap->istate->native_module_cache);
    lu_objectset_add(roots, heap->istate->running_module);
    lu_objectset_add(roots, heap->istate->main_module);
    lu_objectset_add(roots, heap->istate->object_prototype);
    lu_objectset_add(roots, heap->istate->array_prototype);
    lu_objectset_add(roots, heap->istate->string_prototype);

    if (heap->istate->error) {
        lu_objectset_add(roots, heap->istate->error);
    }

    for (uint32_t i = heap->istate->vm->rp; i > 0; --i) {
        struct activation_record* record = &heap->istate->vm->records[i - 1];
        lu_objectset_add(roots, record->executable);
        if (record->function) {
            lu_objectset_add(roots, record->function);
        }
        lu_objectset_add(roots, record->globals->named_slots);
        size_t len = arrlen(record->globals->fast_slots);
        for (uint32_t j = 0; j < len; ++j) {
            if (lu_is_object(record->globals->fast_slots[j])) {
                lu_objectset_add(roots, record->globals->fast_slots[j].object);
            }
        }
        len = arrlen(record->registers);
        for (uint32_t j = 0; j < len; ++j) {
            if (lu_is_object(record->registers[j])) {
                lu_objectset_add(roots, record->registers[j].object);
            }
        }
    }

    for (struct generator* gen = heap->istate->ir_generator; gen; gen = gen->prev) {
        for (size_t i = 0; i < gen->constant_counter; i++) {
            if (lu_is_object(gen->constants[i])) {
                lu_objectset_add(roots, gen->constants[i].object);
            }
        }
    }

    // adding interned strings to roots if any missed
    struct string_map_iter it;
    string_map_iter_init(&it, &heap->istate->string_pool.strings);

    struct string_map_entry* entry;
    while ((entry = string_map_iter_next(&it))!=nullptr) {
        lu_objectset_add(roots, lu_cast(struct lu_object, entry->value));
    }

#ifdef DEBUG
    // printf("Roots collected %ld.\n", hmlen(*roots));
#endif
}

static void collect_live_cells(struct heap* heap,
                               struct lu_objectset* roots,
                               struct lu_objectset* live_cells) {
#ifdef DEBUG
    printf("Collecting live cells...\n");
#endif
    struct lu_objectset_iter iter = lu_objectset_iter_new(roots);
    struct lu_object* obj;
    while ((obj = lu_objectset_iter_next(&iter))!=nullptr) {
        obj->vtable->visit(obj, live_cells);
    }
#ifdef DEBUG
    // printf("Total live cells collected: %ld\n", hmlen(*live_cells));
#endif
}

static void clear_mark_bits(struct heap* heap) {
    for (struct heap_block* block = heap->block_list; block; block = block->next) {
        for (size_t j = 0; j < block->cell_count; ++j) {
            struct lu_object* obj = cell_get(block, j);
            obj->is_marked = false;
        }
    }
}

static void mark_live_cells(struct heap* heap, struct lu_objectset* live_cells) {
    struct lu_objectset_iter iter = lu_objectset_iter_new(live_cells);
    struct lu_object* obj;
    while ((obj = lu_objectset_iter_next(&iter))!=nullptr) {
        printf("Marking live cell: %p name %s\n", obj, obj->vtable->dbg_name);
        obj->is_marked = true;
    }
}

static void block_deallocate_cell(struct heap_block* block, struct lu_object* obj) {
    obj->state = OBJECT_STATE_DEAD;
    obj->next = block->free_list;
    block->free_list = obj;
#ifdef GC_DEBUG_STATS
    g_stats.total_frees++;
#endif
}

static void sweep_dead_cells(struct heap* heap) {
    for (struct heap_block* block = heap->block_list; block; block = block->next) {
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
#ifdef GC_DEBUG_STATS
    clock_t start = clock();
#endif

    struct lu_objectset* roots = lu_objectset_new(16);
    struct lu_objectset* live_cells = lu_objectset_new(16);
    collect_roots(heap, roots);
    collect_live_cells(heap, roots, live_cells);
    clear_mark_bits(heap);
    mark_live_cells(heap, live_cells);

#ifdef GC_DEBUG_STATS
    size_t before_free = g_stats.total_frees;
#endif
    sweep_dead_cells(heap);
// debug
#ifdef GC_DEBUG_STATS
    size_t freed_now = g_stats.total_frees - before_free;
    g_stats.gc_count++;
    g_stats.last_gc_freed = freed_now;
    g_stats.last_gc_live = live_cells->size;
    double secs = (double)(clock() - start) / CLOCKS_PER_SEC;
    fprintf(stderr, "[gc] #%zu done in %.3f s, live=%zu freed=%zu\n", g_stats.gc_count, secs,
            g_stats.last_gc_live, g_stats.last_gc_freed);
#endif

    lu_objectset_free(roots);
    lu_objectset_free(live_cells);
}

void heap_destroy(struct heap* heap) {
#ifdef GC_DEBUG_STATS
    fprintf(stderr, "[heap] destroying heap...\n");
    stats_print_summary(heap);
#endif

    struct heap_block* block = heap->block_list;
    while (block) {
        struct heap_block* next = block->next;
        free(block);
        block = next;
    }
    free(heap);
#ifdef GC_DEBUG_STATS
    fprintf(stderr, "[heap] destroyed, total freed=%zu\n", g_stats.total_frees);
#endif
}
