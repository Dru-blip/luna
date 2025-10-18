#include "runtime/objects/native_function.h"

#include <string.h>

#include "runtime/heap.h"
#include "runtime/istate.h"
#include "strings/interner.h"

lu_type_t* Native_Function_type = nullptr;

void lu_native_function_object_type_new(struct lu_istate* state) {
    Native_Function_type = heap_allocate_object(state->heap, sizeof(lu_type_t));
    Native_Function_type->name_strobj = lu_intern_string(
        state->string_pool, "native_function", strlen("native_function"));
    Native_Function_type->finalize = object_default_finalize;
    Native_Function_type->visit = object_default_visit;
    Native_Function_type->type = Base_type;
}

lu_native_function_object_t* lu_native_function_create(struct lu_istate* state,
                                                       lu_native_func func) {
    //
    lu_native_function_object_t* obj =
        heap_allocate_object(state->heap, sizeof(lu_native_function_object_t));
    obj->type = Native_Function_type;
    obj->func = func;
    return obj;
}
