#pragma once

#include "runtime/istate.h"
#include "runtime/object.h"
#include "strings/view.h"

typedef enum {
    lu_string_flat,
    lu_string_rope,
} lu_string_kind_t;

typedef struct lu_string {
    LUNA_OBJECT_HEADER;
    lu_string_kind_t kind;
    size_t length;
    union {
        struct lu_string *left, *right;
        string_view_t data;
    };
} lu_string_t;

extern lu_type_t* Str_type;

lu_type_t* lu_string_type_object_new(lu_istate_t* state);
lu_string_t* lu_new_string(lu_istate_t* state, string_view_t* view);
lu_object_t* lu_string_concat(lu_istate_t* state, lu_object_t* a,
                              lu_object_t* b);
lu_object_t* lu_string_eq(lu_istate_t* state, lu_object_t* a, lu_object_t* b);

lu_object_t* lu_rope_string_flatten(lu_istate_t* state, lu_string_t* a);
