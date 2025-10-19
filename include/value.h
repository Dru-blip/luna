#pragma once

#include <stddef.h>
#include <stdint.h>

#include "eval.h"

enum lu_value_type {
    VALUE_TRUE,
    VALUE_FALSE,
    VALUE_NULL,
    VALUE_UNDEFINED,
    VALUE_INTEGER,
    VALUE_OBJECT,
};

struct lu_value {
    enum lu_value_type type;
    union {
        int64_t integer;
        struct lu_object* obj;
    };
};

enum lu_object_type {
    OBJECT_INTEGER,
    OBJECT_STRING,
    OBJECT_KLASS,
    OBJECT_DICT,
    OBJECT_FUNC,
};

enum lu_object_state {
    OBJECT_STATE_ALIVE,
    OBJECT_STATE_DEAD,
};

#define LUNA_OBJECT_HEADER      \
    struct lu_object* next;     \
    enum lu_object_type type;   \
    bool is_marked;             \
    enum lu_object_state state; \
    struct lu_klass* klass;

struct lu_object {
    LUNA_OBJECT_HEADER;
};

struct argument {
    struct lu_string* name;
    struct lu_value value;
};

typedef void (*finalize_func_t)(struct lu_object*);
typedef struct lu_value (*native_func_t)(struct lu_istate*, struct lu_value*,
                                         struct argument*);

struct lu_klass {
    LUNA_OBJECT_HEADER;
    finalize_func_t finalize;
    struct lu_string* name;
    struct lu_dict* methods;
    char* dbg_name;
};

struct lu_string {
    LUNA_OBJECT_HEADER;
    size_t length;
    size_t hash;
    char* data;
};

struct lu_dict_entry {
    struct lu_dict_entry* next;
    struct lu_dict_entry* prev;
    struct lu_value key;
    struct lu_value value;
};

struct lu_dict {
    LUNA_OBJECT_HEADER;
    size_t size;
    size_t capacity;
    struct lu_dict_entry** entries;
};

enum lu_function_type {
    FUNCTION_TYPE_NATIVE,
};

struct lu_function {
    LUNA_OBJECT_HEADER;
    enum lu_function_type f_type;
    native_func_t native_func;
};

struct lu_istate;

static inline void lu_klass_default_finalize(struct lu_object* obj) {}

void lu_init_core_klasses(struct lu_istate* state);
struct lu_klass* lu_klass_new(struct lu_istate* state, const char* name);
struct lu_klass* lu_klass_new_with_super(struct lu_istate* state,
                                         const char* name,
                                         struct lu_klass* super);
// string creation

struct lu_string* lu_string_new(struct lu_istate* state, char* str);

// dict methods
struct lu_dict* lu_dict_new(struct lu_istate* state);
struct lu_value lu_dict_get(struct lu_dict* dict, struct lu_value key);
void lu_dict_put(struct lu_istate* state, struct lu_dict* dict,
                 struct lu_value key, struct lu_value value);
struct lu_value lu_dict_remove(struct lu_dict* dict, struct lu_value key);

// function object creation
struct lu_function* lu_native_function_new(struct lu_istate* state,
                                           native_func_t native_func);

void lu_bind_function(struct lu_istate* state, struct lu_klass* klass,
                      char* name, struct lu_function* func);

// Integer class methods

void lu_integer_bind_methods(struct lu_istate* state);

// utilities
#define LUVALUE_NULL ((struct lu_value){.type = VALUE_NULL})
#define LUVALUE_UNDEFINED ((struct lu_value){.type = VALUE_UNDEFINED})

#define LUVALUE_INT(v) \
    ((struct lu_value){.type = VALUE_INTEGER, .integer = (v)})

#define LUVALUE_TRUE ((struct lu_value){.type = VALUE_TRUE, .integer = true})
#define LUVALUE_FALSE ((struct lu_value){.type = VALUE_FALSE, .integer = false})
#define LUVALUE_OBJ(object) \
    ((struct lu_value){.type = VALUE_OBJECT, .obj = object})

static inline struct lu_value luvalue_null() {
    return (struct lu_value){.type = VALUE_NULL};
}

static inline struct lu_value luvalue_undefined() {
    return (struct lu_value){.type = VALUE_UNDEFINED};
}

static inline struct lu_value luvalue_int(int64_t value) {
    return (struct lu_value){.type = VALUE_INTEGER, .integer = value};
}

static inline struct lu_value luvalue_true() {
    return (struct lu_value){.type = VALUE_TRUE, .integer = true};
}

static inline struct lu_value luvalue_false() {
    return (struct lu_value){.type = VALUE_FALSE, .integer = false};
}

bool lu_value_equal(struct lu_value a, struct lu_value b);
