#pragma once

#include "runtime/istate.h"
#include "runtime/object.h"
#include "runtime/objects/strobj.h"

typedef struct lu_argument {
    lu_string_t* name;
    lu_object_t value;
} lu_argument_t;

typedef struct lu_function_object {
    LUNA_OBJECT_HEADER;
    ast_node_t** params;
    ast_node_t* body;
    lu_string_t* name;
} lu_function_object_t;

extern lu_type_t* Function_type;

void lu_function_object_type_new(lu_istate_t* state);
lu_function_object_t* lu_function_create(lu_istate_t* state,
                                         ast_node_t** params, ast_node_t* body);
