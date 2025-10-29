#include "value.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ansi_color_codes.h"
#include "arena.h"
#include "ast.h"
#include "bytecode/interpreter.h"
#include "bytecode/vm.h"
#include "heap.h"
#include "stb_ds.h"
#include "strbuf.h"
#include "string_interner.h"
#include "worklist.h"

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

static void lu_object_finalize(struct lu_object* self) {
    lu_property_map_deinit(&self->properties);
}

static void lu_object_visit(struct lu_object* self, struct lu_objectset* set) {
    struct worklist worklist;
    worklist.head = worklist.tail = nullptr;
    worklist_enqueue(&worklist, self);
    struct property_map_iter iter;
    struct property_map_entry* entry;
    while (worklist.head) {
        struct lu_object* curr = worklist_dequeue(&worklist);
        lu_objectset_add(set, curr);
        iter = property_map_iter_new(&curr->properties);
        while ((entry = property_map_iter_next(&iter)) != nullptr) {
            worklist_enqueue(&worklist, lu_cast(struct lu_object, entry->key));
            if (lu_is_object(entry->value)) {
                worklist_enqueue(&worklist, lu_as_object(entry->value));
            }
        }
    }
}

static void lu_function_visit(struct lu_object* self,
                              struct lu_objectset* set) {
    lu_objectset_add(set, lu_cast(struct lu_function, self)->name);
    lu_object_visit(self, set);
}

static void lu_string_finalize(struct lu_object* self) {
    struct lu_string* str = (struct lu_string*)self;
    if (str->type == STRING_SIMPLE) {
        struct string_block* block = str->block;
        struct string_block* prev = block->prev;

        prev->next = block->next;
        prev->next->prev = prev;

        free(block);
    }
    lu_object_finalize(self);
}

static void lu_array_finalize(struct lu_object* self) {
    struct lu_array* array = lu_cast(struct lu_array, self);
    free(array->elements);
    lu_object_finalize(self);
}

static void lu_array_visit(struct lu_object* self, struct lu_objectset* set) {
    struct lu_array* array = lu_cast(struct lu_array, self);
    for (size_t i = 0; i < array->size; i++) {
        if (lu_is_object(array->elements[i])) {
            lu_objectset_add(set, lu_as_object(array->elements[i]));
        }
    }
    lu_object_visit(self, set);
}

static void lu_module_visit(struct lu_object* self, struct lu_objectset* set) {
    struct lu_module* module = lu_cast(struct lu_module, self);
    lu_objectset_add(set, module->name);
    if (lu_is_object(module->exported)) {
        lu_objectset_add(set, lu_as_object(module->exported));
    }
    lu_object_visit(self, set);
}

static void lu_module_finalize(struct lu_object* self) {
    struct lu_module* module = lu_cast(struct lu_module, self);
    free(module->program.source);
    arrfree(module->program.tokens);
    arena_destroy(&module->program.allocator);
    lu_object_finalize(self);
}

// Object V-Tables
static struct lu_object_vtable lu_object_default_vtable = {
    .is_function = false,
    .is_string = false,
    .is_array = false,
    .finalize = lu_object_finalize,
    .visit = lu_object_visit,
};

static struct lu_object_vtable lu_string_vtable = {
    .is_function = false,
    .is_string = true,
    .is_array = false,
    .finalize = lu_string_finalize,
    .visit = lu_object_visit,
};

static struct lu_object_vtable lu_function_vtable = {
    .is_function = true,
    .is_string = false,
    .is_array = false,
    .finalize = lu_object_finalize,
    .visit = lu_function_visit,
};

static struct lu_object_vtable lu_array_vtable = {
    .is_function = true,
    .is_string = false,
    .is_array = true,
    .finalize = lu_array_finalize,
    .visit = lu_array_visit,
};

static struct lu_object_vtable lu_module_vtable = {
    .is_function = false,
    .is_string = false,
    .is_array = false,
    .finalize = lu_module_finalize,
    .visit = lu_module_visit,
};

bool lu_string_equal(struct lu_string* a, struct lu_string* b) {
    if (a == b) return true;
    if (a->length != b->length) return false;

    // refactor
    if ((a->type == STRING_SMALL || a->type == STRING_SMALL_INTERNED) &&
        (b->type == STRING_SMALL || b->type == STRING_SMALL_INTERNED)) {
        return memcmp(a->Sms, b->Sms, a->length) == 0;
    }

    if (a->type == STRING_SIMPLE && b->type == STRING_SIMPLE)
        return memcmp(a->block->data, b->block->data, a->length) == 0;

    // will segfault when flow reaches here.
    return strncmp(a->data, b->data, a->length) == 0;
}

static const char* lu_value_type_names[] = {"bool", "none", "undefined",
                                            "integer", "object"};

const char* lu_value_get_type_name(struct lu_value a) {
    if (!lu_is_object(a)) {
        return lu_value_type_names[a.type];
    }

    if (lu_is_array(a)) {
        return "array";
    }

    if (lu_is_string(a)) {
        return "string";
    }

    if (lu_is_function(a)) {
        return "function";
    }

    return "object";
}

void lu_property_map_init(struct property_map* map, size_t capacity) {
    map->capacity = capacity;
    map->size = 0;
    map->entries = calloc(capacity, sizeof(struct property_map_entry));
}

void lu_property_map_deinit(struct property_map* map) {
    map->size = 0;
    map->capacity = 0;
    free(map->entries);
}

static bool lu_property_map_add_entry(struct property_map* map,
                                      struct property_map_entry** entries,
                                      size_t capacity, struct lu_string* key,
                                      struct lu_value value, bool is_resize) {
    size_t index = key->hash & (capacity - 1);

    struct property_map_entry* new_entry =
        malloc(sizeof(struct property_map_entry));
    new_entry->key = key;
    new_entry->value = value;
    new_entry->next = new_entry->prev = nullptr;

    struct property_map_entry* entry = entries[index];

    while (entry) {
        if (lu_string_equal(entry->key, new_entry->key)) {
            entry->value = value;
            free(new_entry);
            return false;
        }
        entry = entry->next;
    }

    new_entry->next = entries[index];
    if (new_entry->next) {
        new_entry->next->prev = new_entry;
    }
    entries[index] = new_entry;

    // find a better way to insert the order list
    if (!is_resize) {
        if (map->tail) {
            map->tail->next_in_order = new_entry;
            map->tail = new_entry;
        } else {
            map->head = map->tail = new_entry;
        }
    }

    return true;
}

static void property_map_resize(struct property_map* map, size_t capacity) {
    size_t new_capacity = capacity;
    struct property_map_entry** new_entries =
        calloc(new_capacity, sizeof(struct property_map_entry));

    for (size_t i = 0; i < map->capacity; i++) {
        struct property_map_entry* entry = map->entries[i];
        while (entry) {
            lu_property_map_add_entry(map, new_entries, new_capacity,
                                      entry->key, entry->value, true);
            entry = entry->next;
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
    if (lu_property_map_add_entry(map, map->entries, map->capacity, key, value,
                                  false)) {
        map->size++;
    };
}

struct lu_value lu_property_map_get(struct property_map* map,
                                    struct lu_string* key) {
    size_t index = (key->hash) & (map->capacity - 1);

    struct property_map_entry* entry = map->entries[index];
    while (entry) {
        if (lu_string_equal(entry->key, key)) {
            return entry->value;
        }
        entry = entry->next;
    }

    return lu_value_undefined();
}
void lu_property_map_remove(struct property_map* map, struct lu_string* key) {}

inline struct lu_object* lu_object_new(struct lu_istate* state) {
    struct lu_object* obj =
        heap_allocate_object(state->heap, sizeof(struct lu_object));
    lu_property_map_init(&obj->properties, 4);
    obj->vtable = &lu_object_default_vtable;
    return obj;
}

inline struct lu_object* lu_object_new_sized(struct lu_istate* state,
                                             size_t size) {
    struct lu_object* obj = heap_allocate_object(state->heap, size);
    lu_property_map_init(&obj->properties, 4);
    obj->vtable = &lu_object_default_vtable;
    return obj;
}

inline struct lu_object_vtable* lu_object_get_default_vtable() {
    return &lu_object_default_vtable;
}

struct lu_string* lu_small_string_new(struct lu_istate* state, char* data,
                                      size_t length, size_t hash) {
    struct lu_string* str =
        lu_object_new_sized(state, sizeof(struct lu_string) + length + 1);
    str->type = STRING_SMALL_INTERNED;
    str->hash = hash;
    str->length = length;
    str->vtable = &lu_string_vtable;
    memcpy(str->Sms, data, length);
    str->Sms[length] = '\0';
    // DANGER: this causes infinite recursion.
    // lu_obj_set(str, lu_intern_string(state, "length"), lu_value_int(length));
    // intern_string -> lu_small_string_new -> lu_intern_string -> this loop
    // continues and never ends
    return str;
}

struct lu_string* lu_string_new(struct lu_istate* state, char* data) {
    size_t len = strlen(data);
    size_t hash = hash_str(data, len);

    bool is_small = len <= STRING_SMALL_MAX_LENGTH;

    size_t required_size = is_small ? sizeof(struct lu_string) + len + 1
                                    : sizeof(struct lu_string);
    struct lu_string* str = lu_object_new_sized(state, required_size);
    str->vtable = &lu_string_vtable;
    str->hash = hash;
    str->length = len;
    if (is_small) {
        str->type = STRING_SMALL;
        // inline if small string
        memcpy(str->Sms, data, len);
        str->Sms[len] = '\0';
    } else {
        struct string_block* block = string_block_new(data, len);
        block->prev = state->string_pool.last_block;
        state->string_pool.last_block = block;
        block->prev->next = block;
        str->type = STRING_SIMPLE;
        str->block = block;
    }
    lu_obj_set(str, lu_intern_string(state, "length"), lu_value_int(len));
    return str;
}

static struct lu_string* lu_integer_to_string(struct lu_istate* state,
                                              int64_t value) {
    char buffer[65];
    snprintf(buffer, sizeof(buffer), "%ld", value);
    buffer[64] = '\0';
    return lu_string_new(state, buffer);
}

static struct lu_string* lu_value_to_string(struct lu_istate* state,
                                            struct lu_value value) {
    switch (value.type) {
        case VALUE_INTEGER: {
            return lu_integer_to_string(state, lu_as_int(value));
        }
        case VALUE_BOOL: {
            return lu_intern_string(state, value.integer ? "true" : "false");
        }
        case VALUE_NONE: {
            return lu_intern_string(state, "none");
        }
        default: {
            return lu_intern_string(state, "(object)");
        }
    }
}

static struct string_block* string_block_raw_new(struct lu_istate* state,
                                                 size_t size) {
    struct string_block* block = malloc(sizeof(struct string_block) + size + 1);
    block->next = block->prev = nullptr;

    block->prev = state->string_pool.last_block;
    state->string_pool.last_block = block;
    block->prev->next = block;
    block->length = size;
    block->data[size] = '\0';
    return block;
}

struct lu_string* lu_string_concat(struct lu_istate* state, struct lu_value lhs,
                                   struct lu_value rhs) {
    struct lu_string* lhs_str = nullptr;
    struct lu_string* rhs_str = nullptr;

    if (lu_is_string(lhs)) {
        lhs_str = lu_as_string(lhs);
    } else {
        lhs_str = lu_value_to_string(state, lhs);
    }

    if (lu_is_string(rhs)) {
        rhs_str = lu_as_string(rhs);
    } else {
        rhs_str = lu_value_to_string(state, rhs);
    }

    size_t len = lhs_str->length + rhs_str->length;
    struct string_block* block = string_block_raw_new(state, len);

    memcpy(block->data, lu_string_get_cstring(lhs_str), lhs_str->length);
    memcpy(block->data + lhs_str->length, lu_string_get_cstring(rhs_str),
           rhs_str->length);

    struct lu_string* str =
        lu_object_new_sized(state, sizeof(struct lu_string));
    str->block = block;
    str->length = len;
    str->vtable = &lu_string_vtable;
    str->hash = hash_str(block->data, len);
    str->type = STRING_SIMPLE;

    return str;
}

struct lu_function* lu_function_new(struct lu_istate* state,
                                    struct lu_string* name,
                                    struct lu_module* module,
                                    struct executable* executable) {
    struct lu_function* func =
        lu_object_new_sized(state, sizeof(struct lu_function));
    func->type = FUNCTION_USER;
    func->name = name;
    func->module = module;
    func->executable = executable;
    func->vtable = &lu_function_vtable;

    return func;
}

struct lu_function* lu_native_function_new(struct lu_istate* state,
                                           struct lu_string* name,
                                           native_func_t native_func,
                                           size_t param_count) {
    struct lu_function* func =
        lu_object_new_sized(state, sizeof(struct lu_function));
    func->type = FUNCTION_NATIVE;
    func->name = name;
    func->func = native_func;
    func->param_count = param_count;
    func->vtable = &lu_function_vtable;

    return func;
}

struct lu_array* lu_array_new(struct lu_istate* state) {
    struct lu_array* array =
        lu_object_new_sized(state, sizeof(struct lu_array));
    array->capacity = 4;
    array->size = 0;
    array->vtable = &lu_array_vtable;
    array->elements = calloc(array->capacity, sizeof(struct lu_value));
    return array;
}

void lu_array_push(struct lu_array* array, struct lu_value value) {
    if (array->size + 1 >= array->capacity) {
        array->capacity *= 2;
        array->elements =
            realloc(array->elements, array->capacity * sizeof(struct lu_value));
    }
    array->elements[array->size++] = value;
}

struct lu_value lu_array_get(struct lu_array* array, size_t index) {
    if (index >= array->size) {
        return lu_value_undefined();
    }
    return array->elements[index];
}

int lu_array_set(struct lu_array* array, size_t index, struct lu_value value) {
    if (index >= array->size) {
        return 1;
    }

    array->elements[index] = value;
    return 0;
}

struct lu_module* lu_module_new(struct lu_istate* state, struct lu_string* name,
                                struct ast_program* program) {
    //
    struct lu_module* mod =
        lu_object_new_sized(state, sizeof(struct lu_module));
    mod->program = *program;
    mod->name = name;
    mod->type = MODULE_USER;
    mod->vtable = &lu_module_vtable;
    mod->exported = lu_value_undefined();

    return mod;
}

void lu_raise_error(struct lu_istate* state, struct lu_string* message,
                    struct span* location) {
    struct lu_object* error = lu_object_new(state);

    char buffer[1024];
    struct strbuf sb;
    strbuf_init_static(&sb, buffer, sizeof(buffer));

    lu_obj_set(error, lu_intern_string(state, "message"),
               lu_value_object(message));

    // // TODO: include call stack information
    const char* line_start =
        state->running_module->program.source + location->start;
    int line_length = location->end - location->start;

    strbuf_append(&sb, "  | \n");
    strbuf_appendf(&sb, "%d | ", location->line);
    strbuf_append_n(&sb, line_start, line_length);
    strbuf_append(&sb, "\n");
    strbuf_append(&sb, "  | \n");

    strbuf_append(&sb, "    ");
    for (size_t i = 0; i < line_length; i++) {
        strbuf_appendf(&sb, "%s^", RED);
    }
    strbuf_appendf(&sb, "%s\n", reset);

    // strbuf_appendf(&sb, "in %s:%d:%d\n",
    //                lu_string_get_cstring(state->running_module->name),
    //                location->line, location->col);

    strbuf_appendf(&sb, "%sTraceback (most recent call first):\n%s", YEL,
                   reset);
    for (size_t i = state->vm->rp; i > 0; i--) {
        struct activation_record* record = &state->vm->records[i - 1];
        struct span* span =
            &record->executable->instructions_span[record->ip - 1];
        strbuf_appendf(&sb, "\tat %s%s%s (%s%s:%d:%d%s)\n", BLU,
                       lu_string_get_cstring(record->executable->name), reset,
                       BLU, record->executable->file_path, span->line,
                       span->col, reset);
    }

    lu_obj_set(error, lu_intern_string(state, "traceback"),
               lu_value_object(lu_string_new(state, buffer)));

    state->error_location = *location;
    state->error = error;
}

// object set implementation
struct lu_objectset* lu_objectset_new(size_t initial_capacity) {
    struct lu_objectset* set = calloc(1, sizeof(*set));
    set->entries = calloc(initial_capacity, sizeof(struct lu_object*));
    set->capacity = initial_capacity;
    return set;
}

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

static void lu_objectset_resize(struct lu_objectset* set, size_t new_cap) {
    struct lu_object** old_entries = set->entries;
    size_t old_cap = set->capacity;

    set->entries = calloc(new_cap, sizeof(struct lu_object*));
    set->capacity = new_cap;
    set->size = 0;

    for (size_t i = 0; i < old_cap; i++) {
        struct lu_object* key = old_entries[i];
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

bool lu_objectset_add(struct lu_objectset* set, struct lu_object* key) {
    if (!key) return false;
    if (((float)(set->size + 1) / set->capacity) >= LU_PROPERTY_MAP_LOAD_FACTOR)
        lu_objectset_resize(set, set->capacity * 2);

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

void lu_objectset_free(struct lu_objectset* set) {
    if (!set) return;
    free(set->entries);
    free(set);
}
