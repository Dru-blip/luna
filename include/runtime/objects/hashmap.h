#pragma once

#include "runtime/istate.h"
#include "runtime/object.h"

typedef struct lu_hashmap_entry {
    LUNA_OBJECT_HEADER;
    struct lu_hashmap_entry* next_entry;
    struct lu_hashmap_entry* prev_entry;
    lu_object_t* key;
    lu_object_t* value;
} lu_hashmap_entry_t;

typedef struct lu_hashmap {
    LUNA_OBJECT_HEADER;
    lu_hashmap_entry_t** entries;
    size_t capacity;
    size_t size;
    double load_factor;
} lu_hashmap_t;

extern lu_type_t* Hashmap_type;
extern lu_type_t* Hashmap_entry_type;

typedef struct lu_hashmap_iter {
    lu_hashmap_t* map;
    size_t current_entry_index;
    lu_hashmap_entry_t* chain;
} lu_hashmap_iter_t;

static inline void lu_hashmap_iter_init(lu_hashmap_iter_t* iter,
                                        lu_hashmap_t* map) {
    iter->map = map;
    iter->current_entry_index = 0;
    iter->chain = nullptr;
}

static inline lu_hashmap_entry_t* lu_hashmap_iter_next(
    lu_hashmap_iter_t* iter) {
    if (!iter->map || iter->current_entry_index >= iter->map->capacity) {
        return nullptr;
    }

    lu_hashmap_entry_t* entry = iter->chain;
    if (!entry) {
        while (iter->current_entry_index < iter->map->capacity) {
            entry = iter->map->entries[iter->current_entry_index++];
            if (entry) {
                break;
            }
        }
        iter->chain = entry;
    } else {
        iter->chain = entry->next_entry;
    }

    return entry;
}

lu_type_t* lu_hashmap_type_object_new(lu_istate_t* state);
lu_hashmap_t* lu_hashmap_new(lu_istate_t* state);
lu_hashmap_entry_t* lu_hashmap_put(lu_istate_t* state, lu_hashmap_t* map,
                                   lu_object_t* key, lu_object_t* value);
lu_object_t* lu_hashmap_get(lu_hashmap_t* map, lu_object_t* key);
// TODO: api for contains,removing an item from map
