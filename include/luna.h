#pragma once

#include <stdbool.h>

#include "bytecode/interpreter.h"
#include "heap.h"
#include "value.h"

typedef struct lu_value lu_value;

#define LU_EXPORT __attribute__((visibility("default")))

#define LU_NATIVE_FN(NAME)                                  \
    LU_EXPORT struct lu_value NAME(struct lu_istate* state, \
                                   struct argument* args)

#define LU_RETURN_NONE() return ((lu_value){VALUE_NONE})
#define LU_RETURN_UNDEF() return ((lu_value){VALUE_UNDEFINED})
#define LU_RETURN_INT(x) \
    return ((lu_value){.type = VALUE_INTEGER, .integer = (x)})
#define LU_RETURN_BOOL(x) \
    return ((lu_value){.type = VALUE_BOOL, .integer = (x)})
#define LU_RETURN_OBJ(x) \
    return ((lu_value){.type = VALUE_OBJECT, .object = (x)})

#define LU_ARG_COUNT(state) ((state)->context_stack->call_stack->arg_count)
#define LU_ARG_GET(args, index) (args[index]).value
