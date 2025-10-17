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

lu_hashmap_t* lu_hashmap_new(lu_istate_t* state);
lu_hashmap_entry_t* lu_hashmap_put(lu_istate_t* state, lu_hashmap_t* map,
                                   lu_object_t* key, lu_object_t* value);
lu_object_t* lu_hashmap_get(lu_hashmap_t* map, lu_object_t* key);
// TODO: api for contains,removing an item from map
