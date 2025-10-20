#pragma once

#include <stddef.h>
#include <stdint.h>

#include "eval.h"
#include "string_interner.h"

enum lu_value_type {
    VALUE_BOOL,
    VALUE_NONE,
    VALUE_UNDEFINED,
    VALUE_INTEGER,
    VALUE_OBJECT,
};

struct lu_value {
    enum lu_value_type type;
    union {
        int64_t integer;
        struct lu_object *object;
    };
};

struct property_map_entry {
    struct lu_string *key;
    struct lu_value value;
    size_t psl;
    bool occupied;
};

struct property_map {
    size_t size;
    size_t capacity;
    struct property_map_entry *entries;
};

enum lu_object_state {
    OBJECT_STATE_DEAD,
    OBJECT_STATE_ALIVE,
};

#define LUNA_OBJECT_HEADER                                                     \
    struct lu_object *next;                                                    \
    bool is_marked;                                                            \
    enum lu_object_state state;                                                \
    struct lu_object_vtable *vtable;                                           \
    struct property_map properties;

struct lu_object {
    LUNA_OBJECT_HEADER;
};

struct lu_object_vtable {
    void (*finalize)(struct lu_object *);
    void (*visit)(struct lu_object *);
};

enum lu_string_type {
    STRING_SIMPLE,
    STRING_SMALL,
    STRING_INTERNED,
    STRING_SMALL_INTERNED,
};

struct lu_string {
    LUNA_OBJECT_HEADER;
    enum lu_string_type type;
    size_t hash;
    size_t length;
    union {
        char *data;
        struct string_block *block;
    };
    char Sms[];
};

#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

static inline uint64_t hash_str(const char *key, size_t len) {
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)(unsigned char)key[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

bool lu_string_equal(struct lu_string *a, struct lu_string *b);
void lu_property_map_init(struct property_map *map, size_t capacity);
void lu_property_map_deinit(struct property_map *map);
void lu_property_map_set(struct property_map *map, struct lu_string *key,
                         struct lu_value value);
struct lu_value lu_property_map_get(struct property_map *map,
                                    struct lu_string *key);
void lu_property_map_remove(struct property_map *map, struct lu_string *key);

#define lu_value_none() ((struct lu_value){VALUE_NONE})
struct lu_object *lu_object_new(struct lu_istate *state);
struct lu_object *lu_object_new_sized(struct lu_istate *state, size_t size);
struct lu_string *lu_string_new(struct lu_istate *state,
                                enum lu_string_type type, char *data);
struct lu_string *lu_small_string_new(struct lu_istate *state, char *data,
                                      size_t length, size_t hash);
