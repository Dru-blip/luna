#pragma once

#include "parser/ast.h"
#include "runtime/object.h"

typedef struct lu_argument {
    struct lu_string* name;
    lu_object_t *value;
} lu_argument_t;

typedef struct lu_function_object {
    LUNA_OBJECT_HEADER;
    ast_node_t** params;
    ast_node_t* body;
    struct lu_string* name;
} lu_function_object_t;

extern lu_type_t* Function_type;

struct lu_istate;

void lu_function_object_type_new(struct lu_istate* state);
lu_function_object_t* lu_function_create(struct lu_istate* state,
                                         ast_node_t** params, ast_node_t* body);
