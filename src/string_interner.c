#include "string_interner.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "eval.h"

#include "value.h"

void string_interner_init(struct lu_istate *state) {
    state->string_pool.string_blocks = nullptr;
    state->string_pool.state = state;
    string_map_init(&state->string_pool.strings);
    arena_init(&state->string_pool.string_map_arena);
}

void string_map_init(struct string_map *map) {
    map->capacity = 4;
    map->size = 0;
    map->entries = calloc(map->capacity, sizeof(struct string_map_entry *));
}

static bool string_map_add_string(struct string_interner *interner,
                                  struct string_map_entry **entries,
                                  size_t capacity, char *key, size_t key_len,
                                  struct lu_string **result) {
    size_t hash = hash_str(key, key_len);
    size_t index = hash & (capacity - 1);

    struct string_map_entry *chain = entries[index];
    while (chain) {
        if (chain->key_len == key_len &&
            memcmp(chain->key, key, key_len) == 0) {
            *result = chain->value;
            return false;
        }
        chain = chain->next;
    }

    struct string_map_entry *new_entry = arena_alloc(
        &interner->string_map_arena, sizeof(struct string_map_entry));
    new_entry->key = key;
    new_entry->value = lu_small_string_new(interner->state, key, key_len, hash);
    new_entry->next = new_entry->prev = nullptr;

    if (entries[index]) {
        new_entry->next = entries[index];
        entries[index]->prev = new_entry;
    }
    entries[index] = new_entry;

    *result = new_entry->value;
    return true;
}

static void string_map_resize(struct string_interner *interner,
                              struct string_map *map, size_t capacity) {
    size_t new_capacity = capacity;
    struct string_map_entry **new_entries =
        calloc(new_capacity, sizeof(struct string_map_entry *));

    for (size_t i = 0; i < map->capacity; i++) {
        struct string_map_entry *chain = map->entries[i];
        while (chain) {
            struct lu_string *res;
            string_map_add_string(interner, new_entries, new_capacity,
                                  chain->key, chain->key_len, &res);
            chain = chain->next;
        }
    }

    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

struct lu_string *string_map_put(struct string_interner *interner,
                                 struct string_map *map, char *key,
                                 size_t key_len) {
    if (((float)(map->size + 1) / map->capacity) >= 0.7) {
        size_t new_capacity = map->capacity * 2;
        string_map_resize(interner, map, new_capacity);
    }
    struct lu_string *string;
    if (string_map_add_string(interner, map->entries, map->capacity, key,
                              key_len, &string)) {
        map->size++;
    };

    return string;
}

struct lu_string *lu_string_intern(struct string_interner *interner,
                                   char *str) {
    // TODO: add checks for strings with length more than 20.
    return string_map_put(interner, &interner->strings, str, strlen(str));
}

struct lu_string *lu_string_new(struct lu_istate *state,
                                enum lu_string_type type, char *data) {
    size_t len = strlen(data);
    size_t hash = hash_str(data, len);

    struct string_block *block = string_block_new(data, len);
    block->next = state->string_pool.string_blocks;
    state->string_pool.string_blocks = block;
    if (block->next) {
        block->next->prev = block;
    }
    struct lu_string *str =
        lu_object_new_sized(state, sizeof(struct lu_string));
    str->type = STRING_SIMPLE;
    str->hash = hash;
    str->length = len;
    str->block = block;
    return str;
}

struct string_block *string_block_new(char *data, size_t length) {
    struct string_block *block =
        malloc(sizeof(struct string_block) + length + 1);
    block->length = length;
    memcpy(block->data, data, length);
    block->data[length] = '\0';
    block->next = block->prev = nullptr;
    return block;
}
