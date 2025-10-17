#include "runtime/objects/function.h"

#include <string.h>

#include "runtime/heap.h"
#include "runtime/object.h"
#include "strings/interner.h"

lu_type_t* Function_type = nullptr;

void lu_function_object_type_new(lu_istate_t* state) {
    Function_type = heap_allocate_object(state->heap, sizeof(lu_type_t));
    Function_type->name_strobj =
        lu_intern_string(state->string_pool, "function", strlen("function"));
    Function_type->finalize = object_default_finalize;
    Function_type->visit = object_default_visit;
    Function_type->type = Base_type;
}
lu_function_object_t* lu_function_create(lu_istate_t* state,
                                         ast_node_t** params,
                                         ast_node_t* body) {
    lu_function_object_t* obj =
        heap_allocate_object(state->heap, sizeof(lu_function_object_t));
    obj->type = Function_type;
    obj->params = params;
    obj->body = body;

    return obj;
}
