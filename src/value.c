#include "value.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "heap.h"

#define LU_DICT_LOAD_FACTOR 0.7
#define LU_DICT_MIN_CAPACITY 16

void lu_init_core_klasses(struct lu_istate* state) {
    state->base_object = lu_klass_new(state, "Object");
    state->base_class =
        lu_klass_new_with_super(state, "Class", state->base_object);

    state->int_class = lu_klass_new_with_super(state, "Int", state->base_class);
    state->dict_class =
        lu_klass_new_with_super(state, "Dict", state->base_class);
    state->str_class = lu_klass_new_with_super(state, "Str", state->base_class);
    state->function_class =
        lu_klass_new_with_super(state, "Function", state->base_class);

    state->base_object->methods = lu_dict_new(state);
    state->base_class->methods = lu_dict_new(state);
    state->dict_class->methods = lu_dict_new(state);
    state->int_class->methods = lu_dict_new(state);
    state->str_class->methods = lu_dict_new(state);

    lu_integer_bind_methods(state);
}

struct lu_klass* lu_klass_new(struct lu_istate* state, const char* name) {
    struct lu_klass* klass =
        heap_allocate_object(state->heap, sizeof(struct lu_klass));

    klass->dbg_name = name;
    // TODO: create string objects
    klass->name = nullptr;
    klass->type = OBJECT_KLASS;
    klass->klass = nullptr;
    klass->finalize = lu_klass_default_finalize;
    return klass;
}

struct lu_klass* lu_klass_new_with_super(struct lu_istate* state,
                                         const char* name,
                                         struct lu_klass* super) {
    struct lu_klass* klass =
        heap_allocate_object(state->heap, sizeof(struct lu_klass));

    klass->dbg_name = name;
    // TODO: create string objects
    klass->name = nullptr;
    klass->type = OBJECT_KLASS;
    klass->klass = super;
    klass->finalize = lu_klass_default_finalize;
    return klass;
}

bool lu_value_equal(struct lu_value a, struct lu_value b) {
    if (a.type != b.type) return false;
    if (a.type == VALUE_INTEGER) return a.integer == b.integer;
    if (a.type != VALUE_OBJECT && b.type != VALUE_OBJECT) return false;
    if (a.obj->klass != b.obj->klass) return false;

    switch (a.obj->type) {
        case OBJECT_STRING: {
            return strcmp(((struct lu_string*)a.obj)->data,
                          ((struct lu_string*)b.obj)->data) == 0;
        }
        default: {
            return false;
        }
    }
}

// Dict implementation
#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

static uint64_t hash_key(const char* key, size_t len) {
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)(unsigned char)key[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

size_t str_hash(struct lu_string* self) {
    return hash_key(self->data, self->length);
}

static size_t hash_integer(int64_t key) {
    uint64_t k = (uint64_t)key;

    k = (~k) + (k << 21);
    k = k ^ (k >> 24);
    k = (k + (k << 3)) + (k << 8);
    k = k ^ (k >> 14);
    k = (k + (k << 2)) + (k << 4);
    k = k ^ (k >> 28);
    k = k + (k << 31);

    return (size_t)k;
}

static size_t hash_object(struct lu_object* obj) {
    // TODO: implement dynamic dispatch to call the object's class-specific
    // hash method instead of using a type switch.
    switch (obj->type) {
        case OBJECT_STRING: {
            return ((struct lu_string*)obj)->hash;
        }
        default: {
            return 0;
        }
    }
}

static size_t hash_value(struct lu_value value) {
    switch (value.type) {
        case VALUE_TRUE: {
            return 1;
        }
        case VALUE_NULL: {
            return 2;
        }
        case VALUE_INTEGER: {
            return hash_integer(value.integer);
        }
        case VALUE_OBJECT: {
            return hash_object(value.obj);
        }
        default: {
            return 0;
        }
    }
}

struct lu_dict* lu_dict_new(struct lu_istate* state) {
    struct lu_dict* dict =
        heap_allocate_object(state->heap, sizeof(struct lu_dict));
    dict->capacity = 0;
    dict->size = 0;
    dict->entries = nullptr;
    dict->klass = state->dict_class;
    dict->type = OBJECT_DICT;
    return dict;
}

static bool dict_add_entry(struct lu_dict_entry** entries, size_t capacity,
                           struct lu_value key, struct lu_value value) {
    size_t hash = hash_value(key);
    size_t index = hash & (capacity - 1);

    struct lu_dict_entry* chain = entries[index];
    while (chain) {
        if (lu_value_equal(chain->key, key)) {
            chain->value = value;
            return false;
        }
        chain = chain->next;
    }

    struct lu_dict_entry* new_entry = malloc(sizeof(struct lu_dict_entry));
    new_entry->key = key;
    new_entry->value = value;
    new_entry->next = new_entry->prev = nullptr;

    if (entries[index]) {
        new_entry->next = entries[index];
        entries[index]->prev = new_entry;
    }
    entries[index] = new_entry;

    return true;
}

static void dict_resize(struct lu_dict* dict, size_t capacity) {
    size_t new_capacity = capacity;
    struct lu_dict_entry** new_entries =
        calloc(new_capacity, sizeof(struct lu_dict_entry*));

    if (dict->capacity > 0) {
        for (size_t i = 0; i < dict->capacity; i++) {
            struct lu_dict_entry* entry = dict->entries[i];
            dict_add_entry(new_entries, new_capacity, entry->key, entry->value);
        }
    }

    if (dict->entries) {
        free(dict->entries);
    }

    dict->entries = new_entries;
    dict->capacity = new_capacity;
}

struct lu_value lu_dict_get(struct lu_dict* dict, struct lu_value key) {
    if (dict->capacity == 0) {
        return LUVALUE_NULL;
    }
    size_t hash = hash_value(key);
    size_t index = hash & (dict->capacity - 1);

    struct lu_dict_entry* chain = dict->entries[index];
    while (chain) {
        if (lu_value_equal(chain->key, key)) {
            return chain->value;
        }
        chain = chain->next;
    }
    return LUVALUE_NULL;
}

void lu_dict_put(struct lu_istate* state, struct lu_dict* dict,
                 struct lu_value key, struct lu_value value) {
    if (((float)(dict->size + 1) / dict->capacity) >= LU_DICT_LOAD_FACTOR) {
        size_t new_capacity = dict->capacity * 2 > LU_DICT_MIN_CAPACITY
                                  ? dict->capacity * 2
                                  : LU_DICT_MIN_CAPACITY;
        dict_resize(dict, new_capacity);
    }

    if (dict_add_entry(dict->entries, dict->capacity, key, value)) {
        dict->size++;
    };
}

struct lu_value lu_dict_remove(struct lu_dict* dict, struct lu_value key) {
    size_t hash = hash_value(key);
    size_t index = hash & (dict->capacity - 1);

    struct lu_dict_entry* chain = dict->entries[index];

    while (chain) {
        if (lu_value_equal(chain->key, key)) {
            struct lu_value value = chain->value;
            struct lu_dict_entry* prev = chain->prev;
            prev->next = chain->next;
            prev->next->prev = prev;
            free(chain);
            dict->size--;
            return value;
        }
        chain = chain->next;
    }
    return LUVALUE_NULL;
}

// function object creation
struct lu_function* lu_native_function_new(struct lu_istate* state,
                                           native_func_t native_func) {
    //
    struct lu_function* func =
        heap_allocate_object(state->heap, sizeof(struct lu_function));
    func->f_type = FUNCTION_TYPE_NATIVE;
    func->native_func = native_func;
    func->klass = state->function_class;
    func->type = OBJECT_FUNC;
    return func;
}

void lu_bind_function(struct lu_istate* state, struct lu_klass* klass,
                      char* name, struct lu_function* func) {
    lu_dict_put(state, klass->methods,
                LUVALUE_OBJ((struct lu_object*)lu_string_new(state, name)),
                LUVALUE_OBJ((struct lu_object*)func));
}

// string object creation
struct lu_string* lu_string_new(struct lu_istate* state, char* str) {
    struct lu_string* string =
        heap_allocate_object(state->heap, sizeof(struct lu_string));
    string->type = OBJECT_STRING;
    string->klass = state->str_class;
    string->length = strlen(str);
    string->data = str;
    string->hash = str_hash(string);
    return string;
}
