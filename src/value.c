#include "value.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "eval.h"
#include "heap.h"
#include "string_interner.h"

#define LU_PROPERTY_MAP_LOAD_FACTOR 0.7
#define LU_PROPERTY_MAP_MIN_CAPACITY 16

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

static void lu_object_finalize(struct lu_object *obj) {
    lu_property_map_deinit(&obj->properties);
}

static void lu_object_visit(struct lu_object *obj) {}

static void lu_string_finalize(struct lu_object *obj) {
    struct lu_string *str = (struct lu_string *)obj;
    if (str->type == STRING_SIMPLE) {
        struct string_block *block = str->block;
        struct string_block *prev = block->prev;

        prev->next = block->next;
        prev->next->prev = prev;

        free(block);
    }
    lu_object_finalize(obj);
}

static struct lu_object_vtable lu_object_default_vtable = {
    .is_function = false,
    .is_string = false,
    .finalize = lu_object_finalize,
    .visit = lu_object_visit,
};

static struct lu_object_vtable lu_string_vtable = {
    .is_function = false,
    .is_string = true,
    .finalize = lu_string_finalize,
    .visit = lu_object_visit,
};

static struct lu_object_vtable lu_function_vtable = {
    .is_function = true,
    .is_string = false,
    .finalize = lu_object_finalize,
    .visit = lu_object_visit,
};

bool lu_string_equal(struct lu_string *a, struct lu_string *b) {
    if (a == b)
        return true;
    if (a->length != b->length)
        return false;

    if ((a->type == STRING_SMALL_INTERNED &&
         b->type == STRING_SMALL_INTERNED) ||
        (a->type == STRING_SMALL && b->type == STRING_SMALL)) {
        return memcmp(a->Sms, b->Sms, a->length) == 0;
    }

    return strncmp(a->block->data, b->block->data, a->length) == 0;
}

void lu_property_map_init(struct property_map *map, size_t capacity) {
    map->capacity = capacity;
    map->size = 0;
    map->entries = calloc(capacity, sizeof(struct property_map_entry));

    for (size_t i = 0; i < capacity; i++) {
        map->entries[i].occupied = false;
    }
}

void lu_property_map_deinit(struct property_map *map) {
    map->size = 0;
    map->capacity = 0;
    free(map->entries);
}

static bool lu_property_map_add_entry(struct property_map_entry *entries,
                                      size_t capacity, struct lu_string *key,
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

static void property_map_resize(struct property_map *map, size_t capacity) {
    size_t new_capacity = capacity;
    struct property_map_entry *new_entries =
        calloc(new_capacity, sizeof(struct property_map_entry));

    for (size_t i = 0; i < map->capacity; i++) {
        struct property_map_entry *entry = &map->entries[i];
        if (entry->occupied) {
            lu_property_map_add_entry(new_entries, new_capacity, entry->key,
                                      entry->value);
        }
    }

    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

void lu_property_map_set(struct property_map *map, struct lu_string *key,
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

struct lu_value lu_property_map_get(struct property_map *map,
                                    struct lu_string *key) {
    size_t index = (key->hash) & (map->capacity - 1);
    size_t current_psl = 0;

    while (map->entries[index].occupied) {
        struct property_map_entry *entry = &map->entries[index];
        if (lu_string_equal(entry->key, key)) {
            return map->entries[index].value;
        }
        if (current_psl > entry->psl) {
            return lu_value_undefined();
        }
        current_psl++;
        index = (index + 1) % map->capacity;
    }

    return lu_value_undefined();
}
void lu_property_map_remove(struct property_map *map, struct lu_string *key) {}

struct lu_object *lu_object_new(struct lu_istate *state) {
    struct lu_object *obj =
        heap_allocate_object(state->heap, sizeof(struct lu_object));
    lu_property_map_init(&obj->properties, 4);
    obj->vtable = &lu_object_default_vtable;
    return obj;
}

struct lu_object *lu_object_new_sized(struct lu_istate *state, size_t size) {
    struct lu_object *obj = heap_allocate_object(state->heap, size);
    lu_property_map_init(&obj->properties, 4);
    obj->vtable = &lu_object_default_vtable;
    return obj;
}

struct lu_string *lu_small_string_new(struct lu_istate *state, char *data,
                                      size_t length, size_t hash) {
    struct lu_string *str =
        lu_object_new_sized(state, sizeof(struct lu_string) + length + 1);
    str->type = STRING_SMALL_INTERNED;
    str->hash = hash;
    str->length = length;
    str->vtable = &lu_string_vtable;
    memcpy(str->Sms, data, length);
    str->Sms[length] = '\0';
    return str;
}

struct lu_string *lu_string_new(struct lu_istate *state, char *data) {
    size_t len = strlen(data);
    size_t hash = hash_str(data, len);

    struct string_block *block = string_block_new(data, len);

    block->prev = state->string_pool.last_block;
    state->string_pool.last_block = block;
    block->prev->next = block;

    struct lu_string *str =
        lu_object_new_sized(state, sizeof(struct lu_string));
    str->type = STRING_SIMPLE;
    str->vtable = &lu_string_vtable;
    str->hash = hash;
    str->length = len;
    str->block = block;
    return str;
}

struct lu_function *lu_function_new(struct lu_istate *state,
                                    struct lu_string *name,
                                    struct ast_node **params,
                                    struct ast_node *body) {
    struct lu_function *func =
        lu_object_new_sized(state, sizeof(struct lu_function));
    func->type = FUNCTION_USER;
    func->name = name;
    func->body = body;
    func->params = params;
    func->vtable = &lu_function_vtable;

    return func;
}

struct lu_function *lu_native_function_new(struct lu_istate *state,
                                           struct lu_string *name,
                                           native_func_t native_func) {
    struct lu_function *func =
        lu_object_new_sized(state, sizeof(struct lu_function));
    func->type = FUNCTION_NATIVE;
    func->name = name;
    func->func = native_func;
    func->vtable = &lu_function_vtable;

    return func;
}
