#include "runtime/objects/strobj.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "operator.h"
#include "runtime/heap.h"
#include "runtime/istate.h"
#include "runtime/object.h"
#include "strings/interner.h"

lu_type_t* Str_type = nullptr;

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

size_t str_hash(lu_object_t* self) {
    lu_string_t* sa = (lu_string_t*)self;
    return hash_key(sa->data.str, sa->length);
}

lu_type_t* lu_string_type_object_new(lu_istate_t* state) {
    lu_type_t* type = heap_allocate_object(state->heap, sizeof(lu_type_t));
    type->name = "str";

    //  Segfault incoming.
    //  cause:
    //  to intern a string, the string type object must already exist, since
    //  interned strings are real string instances. But here we’re still
    //  initializing that very type. so the initialized interned string object
    //  contains a null reference to the string type object which causes a
    //  segfault,when garbage collection is triggered or interpreter or anyother
    //  system that is trying to access the string type object  before it is
    //  fully initialized.
    type->name_strobj = lu_intern_string(state->string_pool, "str", 3);
    type->finalize = object_default_finalize;
    type->visit = object_default_visit;

    type->binop_slots[binary_op_add] = lu_string_concat;
    type->binop_slots[binary_op_eq] = lu_string_eq;

    type->hashfn = str_hash;
    type->type = Base_type;

    Str_type = type;
    return type;
}

lu_string_t* lu_new_string(lu_istate_t* state, string_view_t* view) {
    lu_string_t* new_string =
        heap_allocate_object(state->heap, sizeof(lu_string_t));
    new_string->length = view->len;
    new_string->data = *view;
    new_string->type = Str_type;
    return new_string;
}

lu_object_t* lu_string_concat(lu_istate_t* state, lu_object_t* a,
                              lu_object_t* b) {
    if (a->type != Str_type && b->type != Str_type) {
        state->op_result = op_result_not_implemented;
        return nullptr;
    }
    // implementing ropes
    // will take more time , for now leave it unoptimized by just concatinating
    // buffers.
    // lu_string_t* rope =
    //     (lu_string_t*)heap_allocate_object(state->heap, sizeof(lu_string_t));
    // rope->kind = lu_string_rope;
    lu_string_t* sa = (lu_string_t*)a;
    lu_string_t* sb = (lu_string_t*)b;
    // rope->length = sa->length + sb->length;
    // rope->left = sa;
    // rope->right = sb;
    // return (lu_object_t*)rope;
    char* buffer = malloc(sa->length + sb->length);
    memcpy(buffer, sa->data.str, sa->length);
    memcpy(buffer + sa->length, sb->data.str, sb->length);
    lu_string_t* str =
        lu_intern_string(state->string_pool, buffer, sa->length + sb->length);
    free(buffer);

    return str;
}

lu_object_t* lu_string_eq(lu_istate_t* state, lu_object_t* a, lu_object_t* b) {
    if (a->type != Str_type) {
        state->op_result = op_result_not_implemented;
        return nullptr;
    }
    lu_string_t* sa = (lu_string_t*)a;
    lu_string_t* sb = (lu_string_t*)b;

    // just compare the two flat strings , rope implementation is delayed
    //
    // if (sa->kind == lu_string_flat && sb->kind == lu_string_flat) {
    return sa == sb ? state->true_obj : state->false_obj;
    // }

    //  if (sa->kind != sb->kind) {
    //     if (sa->kind == lu_string_rope)
    //         sa = lu_rope_string_flatten(state, sa);
    //     else
    //         sb = lu_rope_string_flatten(state, sb);
    // }

    // if (sa == sb) {
    //     return state->true_obj;
    // }

    // // TODO: implement rope equality
    // if (sa->length != sb->length) {
    //     return state->false_obj;
    // }
}

static void __flatten_node(lu_string_t* node, char* buffer, size_t* used) {
    if (node->kind == lu_string_flat) {
        memcpy(buffer + *used, node->data.str, node->data.len);
        *used += node->data.len;
    } else {
        __flatten_node(node->left, buffer, used);
        __flatten_node(node->right, buffer, used);
    }
}

lu_object_t* lu_rope_string_flatten(lu_istate_t* state, lu_string_t* a) {
    // maybe maintain auxillary arena for temporary allocations
    // instead of calling malloc and free everytime.
    char* buffer = malloc(a->length);
    size_t used = 0;
    __flatten_node(a, buffer, &used);
    a->kind = lu_string_flat;
    a->left = nullptr;
    a->right = nullptr;
    a = lu_intern_string(state->string_pool, buffer, a->length);
    free(buffer);
    return a;
}
