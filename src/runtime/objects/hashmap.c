#include "runtime/objects/hashmap.h"

#include "runtime/heap.h"
#include "stb_ds.h"

#define LU_HASHMAP_LOAD_FACTOR 0.75f

lu_hashmap_t* lu_hashmap_new(lu_istate_t* state) {
    lu_hashmap_t* map = heap_allocate_object(state->heap, sizeof(lu_hashmap_t));
    map->capacity = 16;
    map->size = 0;
    map->entries = nullptr;
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
