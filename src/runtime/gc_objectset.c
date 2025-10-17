#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/heap.h"
#include "runtime/object.h"

uint64_t lu_ptr_hash(void* k) {
    uintptr_t key = (uintptr_t)k;
    key = (~key) + (key << 21);
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8);
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return key;
}

lu_gc_objectset_t* lu_gc_objectset_new(size_t initial_capacity) {
    lu_gc_objectset_t* set = calloc(1, sizeof(*set));
    set->entries = calloc(initial_capacity, sizeof(void*));
    set->capacity = initial_capacity;
    set->load_factor = 0.75;
    return set;
}

static void lu_gc_objectset_rehash(lu_gc_objectset_t* set, size_t new_cap) {
    lu_object_t** old_entries = set->entries;
    size_t old_cap = set->capacity;

    set->entries = calloc(new_cap, sizeof(lu_object_t*));
    set->capacity = new_cap;
    set->size = 0;

    for (size_t i = 0; i < old_cap; i++) {
        void* key = old_entries[i];
        if (key) {
            size_t mask = new_cap - 1;
            size_t idx = lu_ptr_hash(key) & mask;
            while (set->entries[idx]) idx = (idx + 1) & mask;
            set->entries[idx] = key;
            set->size++;
        }
    }

    free(old_entries);
}

bool lu_gc_objectset_insert(lu_gc_objectset_t* set, void* key) {
    if (!key) return false;
    if (set->size >= (size_t)(set->capacity * set->load_factor))
        lu_gc_objectset_rehash(set, set->capacity * 2);

    size_t mask = set->capacity - 1;
    size_t index = lu_ptr_hash(key) & mask;

    for (;;) {
        void* existing = set->entries[index];
        if (existing == nullptr) {
            set->entries[index] = key;
            set->size++;
            return true;
        }
        if (existing == key) return false;
        index = (index + 1) & mask;
    }
}

bool lu_gc_objectset_contains(lu_gc_objectset_t* set, void* key) {
    size_t mask = set->capacity - 1;
    size_t index = lu_ptr_hash(key) & mask;

    for (;;) {
        void* existing = set->entries[index];
        if (existing == nullptr) return false;
        if (existing == key) return true;
        index = (index + 1) & mask;
    }
}

void lu_gc_objectset_clear(lu_gc_objectset_t* set) {
    memset(set->entries, 0, set->capacity * sizeof(void*));
    set->size = 0;
}

void lu_gc_objectset_free(lu_gc_objectset_t* set) {
    if (!set) return;
    free(set->entries);
    free(set);
}
