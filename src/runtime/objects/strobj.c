#include "runtime/objects/strobj.h"

#include "runtime/istate.h"

lu_type_t* Str_type = nullptr;

lu_type_t* lu_string_type_object_new(lu_istate_t* state) {
    lu_type_t* type = heap_allocate_object(state->heap, sizeof(lu_type_t));
    type->name = "str";
    type->finialize = object_default_finalize;
    Str_type = type;
    return type;
}

lu_string_t* lu_new_string(lu_istate_t* state, string_view_t* view) {
    lu_string_t* new_string =
        heap_allocate_object(state->heap, sizeof(lu_string_t));
    new_string->data = *view;
    return new_string;
}
