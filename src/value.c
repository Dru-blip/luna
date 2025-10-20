#include "value.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "eval.h"
#include "heap.h"

#define LU_PROPERTY_MAP_LOAD_FACTOR 0.7
#define LU_PROPERTY_MAP_MIN_CAPACITY 16

// // Dict implementation
// #define FNV_OFFSET 14695981039346656037UL
// #define FNV_PRIME 1099511628211UL

// static uint64_t hash_key(const char* key, size_t len) {
//     uint64_t hash = FNV_OFFSET;
//     for (size_t i = 0; i < len; i++) {
//         hash ^= (uint64_t)(unsigned char)key[i];
//         hash *= FNV_PRIME;
//     }
//     return hash;
// }

// size_t str_hash(struct lu_string* self) {
//     return hash_key(self->data, self->length);
// }

// static size_t hash_integer(int64_t key) {
//     uint64_t k = (uint64_t)key;

//     k = (~k) + (k << 21);
//     k = k ^ (k >> 24);
//     k = (k + (k << 3)) + (k << 8);
//     k = k ^ (k >> 14);
//     k = (k + (k << 2)) + (k << 4);
//     k = k ^ (k >> 28);
//     k = k + (k << 31);

//     return (size_t)k;
// }

// static size_t hash_object(struct lu_object* obj) {
//     // TODO: implement dynamic dispatch to call the object's class-specific
//     // hash method instead of using a type switch.
//     switch (obj->type) {
//         case OBJECT_STRING: {
//             return ((struct lu_string*)obj)->hash;
//         }
//         default: {
//             return 0;
//         }
//     }
// }

static void lu_object_finalize(struct lu_object* obj) {
    lu_property_map_deinit(&obj->properties);
}

static void lu_object_visit(struct lu_object* obj) {}

static struct lu_object_vtable lu_object_default_vtable = {
    .finalize = lu_object_finalize,
    .visit = lu_object_visit,
};

bool lu_string_equal(struct lu_string* a, struct lu_string* b) {
    if (a == b) return true;
    if (a->length != b->length) return false;

    if (a->type == STRING_SMALL && b->type == STRING_SMALL) {
        return memcmp(a->Sms, b->Sms, a->length) == 0;
    }

    return strcmp(a->data, b->data);
}

void lu_property_map_init(struct property_map* map, size_t capacity) {
    map->capacity = capacity;
    map->size = 0;
    map->entries = calloc(capacity, sizeof(struct property_map_entry));

    for (size_t i = 0; i < capacity; i++) {
        map->entries[i].occupied = false;
    }
}

void lu_property_map_deinit(struct property_map* map) {
    map->size = 0;
    map->capacity = 0;
    free(map->entries);
}

static bool lu_property_map_add_entry(struct property_map_entry* entries,
                                      size_t capacity, struct lu_string* key,
                                      struct lu_value value) {
    size_t index = key->hash & (capacity - 1);

    size_t current_psl = 0;
    struct property_map_entry entry = {
        .key = key, .value = value, .occupied = true};

    while (true) {
        if (!entries[index].occupied) {
            entries[index] = entry;
            return true;
        }

        struct property_map_entry c_entry = entries[index];

        if (lu_string_equal(entry.key, c_entry.key)) {
            entries[index].value = value;
            return false;
        }

        if (c_entry.psl > current_psl) {
            struct property_map_entry tmp = c_entry;
            entry.psl = current_psl;
            entries[index] = entry;
            entry = tmp;
            current_psl = entry.psl;
        }

        index = (index + 1) & (capacity - 1);
        current_psl++;
    }
}

static void property_map_resize(struct property_map* map, size_t capacity) {
    size_t new_capacity = capacity;
    struct property_map_entry* new_entries =
        calloc(new_capacity, sizeof(struct property_map_entry));

    for (size_t i = 0; i < map->capacity; i++) {
        struct property_map_entry* entry = &map->entries[i];
        if (entry->occupied) {
            lu_property_map_add_entry(new_entries, new_capacity, entry->key,
                                      entry->value);
        }
    }

    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

void lu_property_map_set(struct property_map* map, struct lu_string* key,
                         struct lu_value value) {
    if (((float)(map->size + 1) / map->capacity) >=
        LU_PROPERTY_MAP_LOAD_FACTOR) {
        size_t new_capacity = map->capacity * 2;
        property_map_resize(map, new_capacity);
    }
    if (lu_property_map_add_entry(map->entries, map->capacity, key, value)) {
        map->size++;
    };
}
struct lu_value lu_property_map_get(struct property_map* map,
                                    struct lu_string* key) {
    size_t index = (key->hash) & (map->capacity - 1);
    size_t current_psl = 0;

    while (map->entries[index].occupied) {
        struct property_map_entry* entry = &map->entries[index];
        if (lu_string_equal(entry->key, key)) {
            return map->entries[index].value;
        }
        if (current_psl > entry->psl) {
            return lu_value_none();
        }
        current_psl++;
        index = (index + 1) % map->capacity;
    }

    return lu_value_none();
}
void lu_property_map_remove(struct property_map* map, struct lu_string* key) {}

struct lu_object* lu_object_new(struct lu_istate* state) {
    struct lu_object* obj =
        heap_allocate_object(state->heap, sizeof(struct lu_object));
    lu_property_map_init(&obj->properties, 4);
    obj->vtable = &lu_object_default_vtable;
    return obj;
}
