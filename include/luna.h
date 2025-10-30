#pragma once

#include <stdbool.h>

#include "bytecode/interpreter.h"
#include "value.h"

typedef struct lu_value lu_value;

#define LU_EXPORT __attribute__((visibility("default")))

#define LU_NATIVE_FN(NAME)                                                   \
    LU_EXPORT struct lu_value NAME(struct lu_vm* vm, struct lu_object* self, \
                                   struct lu_value* args, uint8_t argc)

#define LU_RETURN_NONE() return ((lu_value){VALUE_NONE})
#define LU_RETURN_UNDEF() return ((lu_value){VALUE_UNDEFINED})
#define LU_RETURN_INT(x) \
    return ((lu_value){.type = VALUE_INTEGER, .integer = (x)})
#define LU_RETURN_BOOL(x) \
    return ((lu_value){.type = VALUE_BOOL, .integer = (x)})
#define LU_RETURN_OBJ(x) \
    return ((lu_value){.type = VALUE_OBJECT, .object = (x)})

#define LU_ARG_GET(args, index) (args[index])

static inline void lu_register_native_fn(struct lu_istate* state,
                                         struct lu_object* obj,
                                         const char* name_str,
                                         native_func_t func, int pc) {
    struct lu_string* fname = lu_intern_string(state, (char*)name_str);
    struct lu_function* fobj = lu_native_function_new(state, fname, func, pc);
    lu_obj_set(obj, fname, lu_value_object((struct lu_object*)fobj));
}
