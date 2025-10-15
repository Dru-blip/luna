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

lu_type_t* lu_string_type_object_new(lu_istate_t* state) {
    lu_type_t* type = heap_allocate_object(state->heap, sizeof(lu_type_t));
    type->name = "str";
    type->name_strobj = lu_intern_string(state->string_pool, "str", 3);
    type->finialize = object_default_finalize;

    type->binop_slots[binary_op_add] = lu_string_concat;
    type->binop_slots[binary_op_eq] = lu_string_eq;

    Str_type = type;
    return type;
}

lu_string_t* lu_new_string(lu_istate_t* state, string_view_t* view) {
    lu_string_t* new_string =
        heap_allocate_object(state->heap, sizeof(lu_string_t));
    new_string->length = view->len;
    new_string->data = *view;
    return new_string;
}

lu_object_t* lu_string_concat(lu_istate_t* state, lu_object_t* a,
                              lu_object_t* b) {
    if (a->type != Str_type && b->type != Str_type) {
        state->op_result = op_result_not_implemented;
        return nullptr;
    }
    lu_string_t* rope =
        (lu_string_t*)heap_allocate_object(state->heap, sizeof(lu_string_t));
    rope->kind = lu_string_rope;
    lu_string_t* sa = (lu_string_t*)a;
    lu_string_t* sb = (lu_string_t*)b;
    rope->length = sa->length + sb->length;
    rope->left = sa;
    rope->right = sb;
    return (lu_object_t*)rope;
}

lu_object_t* lu_string_eq(lu_istate_t* state, lu_object_t* a, lu_object_t* b) {
    if (a->type != Str_type) {
        state->op_result = op_result_not_implemented;
        return nullptr;
    }
    lu_string_t* sa = (lu_string_t*)a;
    lu_string_t* sb = (lu_string_t*)b;

    if (sa->kind == lu_string_flat && sb->kind == lu_string_flat) {
        return sa == sb ? state->true_obj : state->false_obj;
    }

    if (sa->kind != sb->kind) {
        if (sa->kind == lu_string_rope)
            sa = lu_rope_string_flatten(state, sa);
        else
            sb = lu_rope_string_flatten(state, sb);
    }

    if (sa == sb) {
        return state->true_obj;
    }

    // TODO: implement rope equality
    if (sa->length != sb->length) {
        return state->false_obj;
    }
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
    // maybe maintain seperate auxillary arena for temporary allocations
    // instead of calling malloc and free everytime.
    char* buffer = malloc(a->length);
    size_t used = 0;
    __flatten_node(a, buffer, &used);
    a->kind = lu_string_flat;
    a->left = nullptr;
    a->right = nullptr;
    lu_string_t* interned =
        lu_intern_string_lookup(state->string_pool, buffer, a->length);
    if (interned) {
        a = interned;
    } else {
        a = lu_intern_string(state->string_pool, buffer, a->length);
    }
    free(buffer);
    return a;
}
