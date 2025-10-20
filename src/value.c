#include "value.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "heap.h"

#define LU_DICT_LOAD_FACTOR 0.7
#define LU_DICT_MIN_CAPACITY 16

// void sb_init(struct string_buffer* sb) {
//     sb->capacity = 128;
//     sb->len = 0;
//     sb->data = malloc(sb->capacity);
// }

// static void sb_grow(struct string_buffer* sb, size_t extra) {
//     size_t required = sb->len + extra;
//     if (required > sb->capacity) {
//         while (sb->capacity < required) sb->capacity *= 2;
//         // TODO: maybe check for allocation failure.
//         sb->data = realloc(sb->data, sb->capacity);
//     }
// }

// void sb_append_bytes(struct string_buffer* sb, const void* src, size_t len) {
//     sb_grow(sb, len);
//     memcpy(sb->data + sb->len, src, len);
//     sb->len += len;
// }

// void sb_append_str(struct string_buffer* sb, const char* str, size_t str_len) {
//     sb_append_bytes(sb, str, str_len);
// }

// void sb_append_char(struct string_buffer* sb, char c) {
//     sb_append_bytes(sb, &c, 1);
// }

// void sb_append_double(struct string_buffer* sb, double value) {
//     char buffer[64];
//     int written = snprintf(buffer, sizeof(buffer), "%g", value);
//     if (written > 0) sb_append_bytes(sb, buffer, written);
// }

// void sb_null_terminate(struct string_buffer* sb) {
//     sb_grow(sb, 1);
//     sb->data[sb->len] = '\0';
// }

// //

// bool lu_value_equal(struct lu_value a, struct lu_value b) {
//     if (a.type != b.type) return false;
//     if (a.type == VALUE_INTEGER) return a.integer == b.integer;
//     if (a.type != VALUE_OBJECT && b.type != VALUE_OBJECT) return false;
//     return a.obj == b.obj;
// }

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

// static size_t hash_value(struct lu_value value) {
//     switch (value.type) {
//         case VALUE_TRUE: {
//             return 1;
//         }
//         case VALUE_NULL: {
//             return 2;
//         }
//         case VALUE_INTEGER: {
//             return hash_integer(value.integer);
//         }
//         case VALUE_OBJECT: {
//             return hash_object(value.obj);
//         }
//         default: {
//             return 0;
//         }
//     }
// }

// struct lu_dict* lu_dict_new(struct lu_istate* state) {
//     struct lu_dict* dict =
//         heap_allocate_object(state->heap, sizeof(struct lu_dict));
//     dict->capacity = 0;
//     dict->size = 0;
//     dict->entries = nullptr;
//     return dict;
// }

// static bool dict_add_entry(struct lu_dict_entry** entries, size_t capacity,
//                            struct lu_value key, struct lu_value value) {
//     size_t hash = hash_value(key);
//     size_t index = hash & (capacity - 1);

//     struct lu_dict_entry* chain = entries[index];
//     while (chain) {
//         if (lu_value_equal(chain->key, key)) {
//             chain->value = value;
//             return false;
//         }
//         chain = chain->next;
//     }

//     struct lu_dict_entry* new_entry = malloc(sizeof(struct lu_dict_entry));
//     new_entry->key = key;
//     new_entry->value = value;
//     new_entry->next = new_entry->prev = nullptr;

//     if (entries[index]) {
//         new_entry->next = entries[index];
//         entries[index]->prev = new_entry;
//     }
//     entries[index] = new_entry;

//     return true;
// }

// static void dict_resize(struct lu_dict* dict, size_t capacity) {
//     size_t new_capacity = capacity;
//     struct lu_dict_entry** new_entries =
//         calloc(new_capacity, sizeof(struct lu_dict_entry*));

//     if (dict->capacity > 0) {
//         for (size_t i = 0; i < dict->capacity; i++) {
//             struct lu_dict_entry* chain = dict->entries[i];
//             while (chain) {
//                 dict_add_entry(new_entries, new_capacity, chain->key,
//                                chain->value);
//                 chain = chain->next;
//             }
//         }
//     }

//     if (dict->entries) {
//         free(dict->entries);
//     }

//     dict->entries = new_entries;
//     dict->capacity = new_capacity;
// }

// struct lu_value lu_dict_get(struct lu_dict* dict, struct lu_value key) {
//     if (dict->capacity == 0) {
//         return LUVALUE_UNDEFINED;
//     }
//     size_t hash = hash_value(key);
//     size_t index = hash & (dict->capacity - 1);

//     struct lu_dict_entry* chain = dict->entries[index];
//     while (chain) {
//         if (lu_value_equal(chain->key, key)) {
//             return chain->value;
//         }
//         chain = chain->next;
//     }
//     return LUVALUE_UNDEFINED;
// }

// void lu_dict_put(struct lu_istate* state, struct lu_dict* dict,
//                  struct lu_value key, struct lu_value value) {
//     if (((float)(dict->size + 1) / dict->capacity) >= LU_DICT_LOAD_FACTOR) {
//         size_t new_capacity = dict->capacity * 2 > LU_DICT_MIN_CAPACITY
//                                   ? dict->capacity * 2
//                                   : LU_DICT_MIN_CAPACITY;
//         dict_resize(dict, new_capacity);
//     }

//     if (dict_add_entry(dict->entries, dict->capacity, key, value)) {
//         dict->size++;
//     };
// }

// struct lu_value lu_dict_remove(struct lu_dict* dict, struct lu_value key) {
//     size_t hash = hash_value(key);
//     size_t index = hash & (dict->capacity - 1);

//     struct lu_dict_entry* chain = dict->entries[index];

//     while (chain) {
//         if (lu_value_equal(chain->key, key)) {
//             struct lu_value value = chain->value;
//             struct lu_dict_entry* prev = chain->prev;
//             prev->next = chain->next;
//             prev->next->prev = prev;
//             free(chain);
//             dict->size--;
//             return value;
//         }
//         chain = chain->next;
//     }
//     return LUVALUE_NULL;
// }
