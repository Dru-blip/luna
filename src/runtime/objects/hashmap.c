#include "runtime/objects/hashmap.h"

#include "runtime/heap.h"
#include "runtime/object.h"
#include "stb_ds.h"
#include "strings/interner.h"

#define LU_HASHMAP_LOAD_FACTOR 0.75f

lu_type_t* Hashmap_type = nullptr;
lu_type_t* Hashmap_entry_type = nullptr;

static void hashmap_visit(lu_object_t* obj, struct lu_gc_objectset* livecells) {
    lu_gc_objectset_insert(livecells, obj->type);
    lu_hashmap_iter_t iter;
    lu_hashmap_t* map = (lu_hashmap_t*)obj;
    lu_hashmap_iter_init(&iter, map);
    lu_hashmap_entry_t* entry;
    while ((entry = lu_hashmap_iter_next(&iter))) {
        lu_gc_objectset_insert(livecells, entry);
        lu_gc_objectset_insert(livecells, entry->key);
        lu_gc_objectset_insert(livecells, entry->value);
    }
}

static void hashmap_finalize(lu_object_t* obj) {
    lu_hashmap_t* map = (lu_hashmap_t*)obj;
    arrfree(map->entries);
}

lu_type_t* lu_hashmap_type_object_new(lu_istate_t* state) {
    lu_type_t* type = heap_allocate_object(state->heap, sizeof(lu_type_t));
    type->name = "hash";

    type->name_strobj = lu_intern_string(state->string_pool, "hash", 4);

    type->finalize = hashmap_finalize;
    type->visit = hashmap_visit;
    type->type = Base_type;

    Hashmap_type = type;

    lu_type_t* entry_type =
        heap_allocate_object(state->heap, sizeof(lu_type_t));
    entry_type->name = "hash_entry";

    entry_type->name_strobj =
        lu_intern_string(state->string_pool, "hash_entry", 9);
    entry_type->finalize = object_default_finalize;
    entry_type->visit = object_default_visit;
    Hashmap_entry_type = entry_type;

    return type;
}

lu_hashmap_t* lu_hashmap_new(lu_istate_t* state) {
    lu_hashmap_t* map = heap_allocate_object(state->heap, sizeof(lu_hashmap_t));
    map->capacity = 16;
    map->size = 0;
    map->entries = nullptr;
    map->type = Hashmap_type;
    arrsetlen(map->entries, map->capacity);
    memset(map->entries, 0, sizeof(lu_hashmap_entry_t*) * map->capacity);
    return map;
}

static void lu_hashmap_resize(lu_istate_t* state, lu_hashmap_t* map,
                              size_t new_capacity) {
    lu_hashmap_entry_t** new_entries = nullptr;
    arrsetlen(new_entries, new_capacity);
    memset(new_entries, 0, sizeof(lu_hashmap_entry_t*) * new_capacity);

    for (size_t i = 0; i < map->capacity; ++i) {
        lu_hashmap_entry_t* entry = map->entries[i];
        while (entry) {
            lu_hashmap_entry_t* next = entry->next_entry;
            size_t index = entry->hash & (new_capacity - 1);

            entry->next_entry = new_entries[index];
            if (entry->next_entry) entry->next_entry->prev_entry = entry;

            entry->prev_entry = nullptr;
            new_entries[index] = entry;

            entry = next;
        }
    }

    arrfree(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

static lu_hashmap_entry_t* lu_hashmap_add_entry(lu_istate_t* state,
                                                lu_hashmap_t* map,
                                                lu_object_t* key,
                                                lu_object_t* value) {
    if (!key->hash) key->hash = key->type->hashfn(key);

    size_t hash = key->hash;
    size_t index = hash & (map->capacity - 1);

    lu_hashmap_entry_t* chain = map->entries[index];
    for (lu_hashmap_entry_t* e = chain; e; e = e->next_entry) {
        // TODO: implement object comparison
        if (e->key == key) {
            e->value = value;
            return e;
        }
    }

    lu_hashmap_entry_t* new_entry =
        heap_allocate_object(state->heap, sizeof(lu_hashmap_entry_t));
    new_entry->key = key;
    new_entry->value = value;
    new_entry->hash = hash;
    new_entry->prev_entry = NULL;
    new_entry->next_entry = chain;
    new_entry->type = Hashmap_entry_type;

    while (chain) {
        // TODO: implement object comparison
        if (chain->key == key) {
            chain->value = value;
            return chain;
        }
        chain = chain->next_entry;
    }

    if (!chain) {
        map->entries[index] = new_entry;
    } else {
        chain = map->entries[index];
        new_entry->next_entry = chain;
        chain->prev_entry = new_entry;
    }

    map->entries[index] = new_entry;
    map->size++;
    return new_entry;
}

lu_hashmap_entry_t* lu_hashmap_put(lu_istate_t* state, lu_hashmap_t* map,
                                   lu_object_t* key, lu_object_t* value) {
    float load = ((float)map->size + 1) / (float)map->capacity;
    if (load > LU_HASHMAP_LOAD_FACTOR)
        lu_hashmap_resize(state, map, map->capacity * 2);

    return lu_hashmap_add_entry(state, map, key, value);
}

lu_object_t* lu_hashmap_get(lu_hashmap_t* map, lu_object_t* key) {
    if (!key->hash) key->hash = key->type->hashfn(key);

    size_t hash = key->hash;
    size_t index = hash & (map->capacity - 1);

    lu_hashmap_entry_t* chain = map->entries[index];
    for (lu_hashmap_entry_t* e = chain; e; e = e->next_entry) {
        // TODO: implement compare function
        if (e->key == key) {
            return e->value;
        }
    }

    return nullptr;
}
