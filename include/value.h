#pragma once

#include <asm-generic/errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ast.h"
#include "string_interner.h"

enum lu_value_type {
    VALUE_BOOL,
    VALUE_NONE,
    // used only for internal purposes , runtime will not
    // expose this value to the user.
    VALUE_UNDEFINED,
    VALUE_INTEGER,
    VALUE_OBJECT,
};

struct lu_value {
    enum lu_value_type type;
    union {
        int64_t integer;
        struct lu_object* object;
    };
};

struct property_map_entry {
    struct lu_string* key;
    struct lu_value value;
    struct property_map_entry *next, *prev;
    struct property_map_entry* next_in_order;
};

struct property_map {
    size_t size;
    size_t capacity;
    struct property_map_entry** entries;
    struct property_map_entry* head;
    struct property_map_entry* tail;
};

struct property_map_iter {
    struct property_map* map;
    size_t index;
    struct property_map_entry* chain;
};

enum lu_object_state {
    OBJECT_STATE_DEAD,
    OBJECT_STATE_ALIVE,
};

#define LUNA_OBJECT_HEADER           \
    struct lu_object* next;          \
    bool is_marked;                  \
    enum lu_object_state state;      \
    struct lu_object_vtable* vtable; \
    struct property_map properties;  \
    bool is_sealed;                  \
    struct lu_object* prototype;

struct lu_object {
    LUNA_OBJECT_HEADER;
};

struct lu_objectset {
    struct lu_object** entries;
    size_t capacity;
    size_t size;
};

struct lu_objectset_iter {
    struct lu_objectset* set;
    size_t index;
};

enum object_tag {
    OBJECT_TAG_FUNCTION,
    OBJECT_TAG_STRING,
    OBJECT_TAG_ARRAY,
    OBJECT_TAG_EXECUTABLE,
};

struct lu_object_vtable {
    bool is_function;
    bool is_string;
    bool is_array;
    enum object_tag tag;
    const char* dbg_name;
    void (*finalize)(struct lu_object*);
    void (*visit)(struct lu_object*, struct lu_objectset*);
};

enum lu_string_type {
    STRING_SIMPLE,
    STRING_SMALL,
    STRING_INTERNED,
    STRING_SMALL_INTERNED,
};

#define STRING_SMALL_MAX_LENGTH 31

struct lu_string {
    LUNA_OBJECT_HEADER;
    enum lu_string_type type;
    size_t hash;
    size_t length;
    union {
        char* data;  // unused field reserved for future use
        struct string_block* block;
    };
    char Sms[];
};

enum lu_function_type {
    FUNCTION_USER,
    FUNCTION_NATIVE,
};

struct argument {
    struct lu_string* name;
    struct lu_value value;
};

struct lu_vm;

typedef struct lu_value (*native_func_t)(struct lu_vm*, struct lu_object*,
                                         struct lu_value*, uint8_t);

struct lu_function {
    LUNA_OBJECT_HEADER;
    enum lu_function_type type;
    struct lu_string* name;
    struct lu_module* module;
    size_t param_count;
    union {
        native_func_t func;
        struct executable* executable;
        struct {
            struct ast_node** params;
            struct ast_node* body;
        };
    };
};

enum lu_module_type {
    MODULE_USER,
    MODULE_NATIVE,
};

struct lu_module {
    LUNA_OBJECT_HEADER;
    enum lu_module_type type;
    struct lu_string* name;
    union {
        struct ast_program program;
        void* module_handle;
    };

    struct lu_value exported;
};

typedef void (*module_init_func)(struct lu_istate*, struct lu_module*);

struct lu_array {
    LUNA_OBJECT_HEADER;
    size_t size;
    size_t capacity;
    struct lu_value* elements;
};

struct lu_array_iter {
    struct lu_array* array;
    size_t index;
};

// all macros normally follows SCREAMING_SNAKE_CASE naming convention but these
// are the only macro defs dont follow the convention.
#define lu_cast(T, obj) ((T*)(obj))
#define lu_value_none() ((struct lu_value){VALUE_NONE})
#define lu_value_undefined() ((struct lu_value){VALUE_UNDEFINED})
#define lu_value_int(v) ((struct lu_value){.type = VALUE_INTEGER, .integer = v})
#define lu_value_bool(v) ((struct lu_value){.type = VALUE_BOOL, .integer = v})
#define lu_value_object(v) \
    ((struct lu_value){.type = VALUE_OBJECT, .object = v})

#define lu_is_bool(v) ((v).type == VALUE_BOOL)
#define lu_is_none(v) ((v).type == VALUE_NONE)
#define lu_is_undefined(v) ((v).type == VALUE_UNDEFINED)
#define lu_is_int(v) ((v).type == VALUE_INTEGER)
#define lu_is_object(v) ((v).type == VALUE_OBJECT)
#define lu_is_function(v) \
    (lu_is_object(v) && lu_as_object(v)->vtable->is_function)
#define lu_is_string(v) (lu_is_object(v) && lu_as_object(v)->vtable->is_string)
#define lu_is_array(v) (lu_is_object(v) && lu_as_object(v)->vtable->is_array)
#define lu_is_executable(v) \
    (lu_is_object(v) && lu_as_object(v)->vtable->tag == OBJECT_TAG_EXECUTABLE)

#define lu_as_int(v) ((v).integer)
#define lu_as_function(v) ((struct lu_function*)lu_as_object(v))
#define lu_as_object(v) ((v).object)
#define lu_as_string(v) ((struct lu_string*)lu_as_object(v))
#define lu_as_array(v) ((struct lu_array*)lu_as_object(v))

#define lu_obj_get(obj, key) lu_object_get_property(obj, key)
#define lu_obj_set(obj, key, value) \
    lu_property_map_set(&(obj)->properties, key, value)
#define lu_obj_remove(obj, key) lu_property_map_remove(&(obj)->properties, key)

#define lu_obj_size(obj) (obj)->properties.size
#define lu_array_length(array) ((array)->size)

#define lu_is_falsy(val) \
    (val.type == VALUE_BOOL && val.integer == false) || (val.type == VALUE_NONE)
#define lu_is_truthy(val) !lu_is_falsy(val)

static inline struct lu_value lu_bool(bool v) { return lu_value_bool(v); }
static inline struct lu_value lu_int(int64_t v) { return lu_value_int(v); }
static inline struct lu_value lu_none(void) { return lu_value_none(); }
static inline struct lu_value lu_undefined(void) {
    return lu_value_undefined();
}

#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

static inline uint64_t hash_str(const char* key, size_t len) {
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)(unsigned char)key[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

const char* lu_value_get_type_name(struct lu_value a);
bool lu_string_equal(struct lu_string* a, struct lu_string* b);

void lu_property_map_init(struct property_map* map, size_t capacity);
void lu_property_map_deinit(struct property_map* map);
void lu_property_map_set(struct property_map* map, struct lu_string* key,
                         struct lu_value value);
bool lu_property_map_has(struct property_map* map, struct lu_string* key);
struct lu_value lu_property_map_get(struct property_map* map,
                                    struct lu_string* key);

// TODO: implement property remove
void lu_property_map_remove(struct property_map* map, struct lu_string* key);

struct lu_object* lu_object_new(struct lu_istate* state);
struct lu_object* lu_object_new_sized(struct lu_istate* state, size_t size);
struct lu_value lu_object_get_property(struct lu_object* obj,
                                       struct lu_string* key);
struct lu_object_vtable* lu_object_get_default_vtable();
struct lu_string* lu_string_new(struct lu_istate* state, char* data);
struct lu_string* lu_small_string_new(struct lu_istate* state, char* data,
                                      size_t length, size_t hash);
struct lu_string* lu_string_concat(struct lu_istate* state, struct lu_value lhs,
                                   struct lu_value rhs);

struct lu_function* lu_function_new(struct lu_istate* state,
                                    struct lu_string* name,
                                    struct lu_module* module,
                                    struct executable* executable);

struct lu_function* lu_native_function_new(struct lu_istate* state,
                                           struct lu_string* name,
                                           native_func_t native_func,
                                           size_t param_count);

struct lu_module* lu_module_new(struct lu_istate* state, struct lu_string* name,
                                struct ast_program* program);

struct lu_array* lu_array_new(struct lu_istate* state);
void lu_array_push(struct lu_array* array, struct lu_value value);
struct lu_value lu_array_get(struct lu_array* array, size_t index);
int lu_array_set(struct lu_array* array, size_t index, struct lu_value value);

void lu_raise_error(struct lu_istate* state, struct lu_string* message,
                    struct span* location);

void lu_init_global_object(struct lu_istate* state);

struct lu_objectset* lu_objectset_new(size_t initial_capacity);
bool lu_objectset_add(struct lu_objectset* set, struct lu_object* key);
void lu_objectset_free(struct lu_objectset* set);

static inline struct lu_objectset_iter lu_objectset_iter_new(
    struct lu_objectset* set) {
    struct lu_objectset_iter iter = {set, 0};
    return iter;
}

static inline struct lu_object* lu_objectset_iter_next(
    struct lu_objectset_iter* iter) {
    while (iter->index < iter->set->capacity) {
        struct lu_object* key = iter->set->entries[iter->index++];
        if (key) return key;
    }
    return nullptr;
}

static inline struct property_map_iter property_map_iter_new(
    struct property_map* map) {
    struct property_map_iter iter = {map, 0};
    iter.chain = map->head;
    return iter;
}

static inline struct property_map_entry* property_map_iter_next(
    struct property_map_iter* iter) {
    struct property_map_entry* current = iter->chain;
    if (current) {
        iter->chain = current->next_in_order;
    }
    return current;
}

static inline char* lu_string_get_cstring(struct lu_string* str) {
    if (str->type == STRING_SMALL_INTERNED || str->type == STRING_SMALL) {
        return str->Sms;
    }

    if (str->type == STRING_SIMPLE || str->type == STRING_INTERNED) {
        return str->block->data;
    }

    return str->data;
}

static inline struct lu_array_iter lu_array_iter_new(struct lu_array* array) {
    struct lu_array_iter iter = {array, 0};
    return iter;
}

static inline struct lu_value lu_array_iter_next(struct lu_array_iter* iter) {
    if (iter->index < iter->array->size) {
        return iter->array->elements[iter->index++];
    }
    return lu_value_undefined();
}

static inline int64_t lu_strcmp(struct lu_string* a, struct lu_string* b) {
    size_t min_len = a->length > b->length ? b->length : a->length;
    char* adata = lu_string_get_cstring(a);
    char* bdata = lu_string_get_cstring(b);
    int64_t cmp_res = memcmp(adata, bdata, min_len);
    if (cmp_res != 0) return cmp_res;
    return a->length < b->length ? -1 : a->length > b->length;
}

static inline bool lu_value_strict_equals(struct lu_value a,
                                          struct lu_value b) {
    // strict comparison
    if (a.type != b.type) return false;
    switch (a.type) {
        case VALUE_BOOL:
        case VALUE_INTEGER:
            return a.integer == b.integer;
        case VALUE_NONE:
        case VALUE_UNDEFINED:
            return true;
        case VALUE_OBJECT: {
            if (lu_is_string(a)) {
                return lu_string_equal(lu_as_string(a), lu_as_string(b));
            }
            return a.object == b.object;
        }
        default:
            return false;
    }
}
