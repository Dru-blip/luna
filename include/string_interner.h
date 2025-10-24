#pragma once

#include <stddef.h>

#include "arena.h"

struct string_block {
    struct string_block *next, *prev;
    size_t length;
    char data[];
};

struct string_map_entry {
    char* key;
    size_t key_len;
    struct lu_string* value;
    struct string_map_entry *next, *prev;
};

struct string_map {
    size_t size;
    size_t capacity;
    struct string_map_entry** entries;
};

struct string_map_iter {
    struct string_map* map;
    size_t entry_index;
    struct string_map_entry* current;
};

struct string_interner {
    struct string_block* first_block;
    struct string_block* last_block;

    struct string_map strings;
    struct arena string_map_arena;
    struct lu_istate* state;
};

void string_interner_init(struct lu_istate* state);
void string_interner_destroy(struct string_interner* interner);

void string_map_init(struct string_map* map);

struct lu_string* string_map_put(struct string_interner* interner,
                                 struct string_map* map, char* key,
                                 size_t key_len);
struct lu_string* lu_intern_string(struct lu_istate* state, char* str);
struct string_block* string_block_new(char* data, size_t length);

static inline void string_map_iter_init(struct string_map_iter* it,
                                        struct string_map* map) {
    it->map = map;
    it->entry_index = 0;
    it->current = nullptr;
    while (it->entry_index > 0 && it->entry_index < map->capacity &&
           !it->current) {
        if (it->map->entries[it->entry_index]) {
            it->current = it->map->entries[it->entry_index];
            break;
        }
        it->entry_index++;
    }
}

static inline struct string_map_entry* string_map_iter_next(
    struct string_map_iter* it) {
    if (!it->current) return nullptr;

    struct string_map_entry* entry = it->current;

    if (entry->next) {
        it->current = entry->next;
        return entry;
    }

    it->entry_index++;
    while (it->entry_index < it->map->capacity) {
        if (it->map->entries[it->entry_index]) {
            it->current = it->map->entries[it->entry_index];
            return entry;
        }
        it->entry_index++;
    }

    it->current = nullptr;
    return entry;
}
