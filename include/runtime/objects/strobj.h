#pragma once

#include "runtime/istate.h"
#include "runtime/object.h"
#include "strings/view.h"

typedef struct lu_string {
    LUNA_OBJECT_HEADER;
    string_view_t data;
} lu_string_t;

extern lu_type_t* Str_type;

lu_type_t* lu_string_type_object_new(lu_istate_t* state);
lu_string_t* lu_new_string(lu_istate_t* state, string_view_t* view);
