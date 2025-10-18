#pragma once

#include "runtime/objects/function.h"

struct lu_istate;

typedef lu_object_t* (*lu_native_func)(struct lu_istate*, lu_object_t*,
                                       lu_argument_t*);

typedef struct lu_native_function_object {
    LUNA_OBJECT_HEADER;
    lu_native_func func;
} lu_native_function_object_t;

extern lu_type_t* Native_Function_type;

void lu_native_function_object_type_new(struct lu_istate* state);
lu_native_function_object_t* lu_native_function_create(struct lu_istate* state,
                                                       lu_native_func func);
