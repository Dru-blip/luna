#include <stdio.h>

#include "eval.h"
#include "value.h"

#define LU_INT_ARITH(name, op)                                              \
    struct lu_value name(struct lu_istate* state, struct lu_object* self,   \
                         struct argument* args) {                           \
        if (args[1].value.type != VALUE_INTEGER) {                          \
            return lu_raise_error(state, "TypeError",                       \
                                  "Expected integer argument");             \
        }                                                                   \
        return luvalue_int(args[0].value.integer op args[1].value.integer); \
    }

#define LU_INT_DIV(name, op)                                                \
    struct lu_value name(struct lu_istate* state, struct lu_object* self,   \
                         struct argument* args) {                           \
        if (args[1].value.type != VALUE_INTEGER) {                          \
            return lu_raise_error(state, "TypeError",                       \
                                  "Expected integer argument");             \
        }                                                                   \
        if (args[1].value.integer == 0) {                                   \
            return lu_raise_error(state, "ZeroDivisionError",               \
                                  "Cannot divide by zero");                 \
        }                                                                   \
        return luvalue_int(args[0].value.integer op args[1].value.integer); \
    }

#define LU_INT_COMPARE(name, op)                                               \
    struct lu_value name(struct lu_istate* state, struct lu_object* self,      \
                         struct argument* args) {                              \
        if (args[1].value.type != VALUE_INTEGER) {                             \
            return lu_raise_error(state, "TypeError",                          \
                                  "Expected integer argument");                \
        }                                                                      \
        return args[0].value.integer op args[1].value.integer ? LUVALUE_TRUE   \
                                                              : LUVALUE_FALSE; \
    }

LU_INT_ARITH(lu_int_add, +);
LU_INT_ARITH(lu_int_sub, -);
LU_INT_ARITH(lu_int_mul, *);

LU_INT_DIV(lu_int_div, /);
LU_INT_DIV(lu_int_mod, %);

LU_INT_COMPARE(lu_int_lt, <);
LU_INT_COMPARE(lu_int_lte, <=);
LU_INT_COMPARE(lu_int_gt, >);
LU_INT_COMPARE(lu_int_gte, >=);
LU_INT_COMPARE(lu_int_eq, ==);
LU_INT_COMPARE(lu_int_neq, !=);

struct lu_value lu_int_negate(struct lu_istate* state, struct lu_object* self,
                              struct argument* args) {
    return LUVALUE_INT(-args[0].value.integer);
}

struct lu_value lu_int_lnot(struct lu_istate* state, struct lu_object* self,
                            struct argument* args) {
    return args[0].value.integer ? LUVALUE_FALSE : LUVALUE_TRUE;
}

struct lu_value lu_int_to_str(struct lu_istate* state, struct lu_object* self,
                              struct argument* args) {
    //
    size_t size = snprintf(NULL, 0, "%ld", args[0].value.integer);
    char* buffer = malloc(size + 1);
    snprintf(buffer, size + 1, "%ld", args[0].value.integer);
    struct lu_value result =
        LUVALUE_OBJ((struct lu_object*)lu_string_new(state, buffer));
    free(buffer);
    return result;
}

void lu_integer_bind_methods(struct lu_istate* state) {
    lu_bind_function(state, state->int_class, "add",
                     lu_native_function_new(state, lu_int_add));

    lu_bind_function(state, state->int_class, "sub",
                     lu_native_function_new(state, lu_int_sub));

    lu_bind_function(state, state->int_class, "mul",
                     lu_native_function_new(state, lu_int_mul));

    lu_bind_function(state, state->int_class, "div",
                     lu_native_function_new(state, lu_int_div));
    lu_bind_function(state, state->int_class, "mod",
                     lu_native_function_new(state, lu_int_mod));

    lu_bind_function(state, state->int_class, "lt",
                     lu_native_function_new(state, lu_int_lt));

    lu_bind_function(state, state->int_class, "lte",
                     lu_native_function_new(state, lu_int_lte));
    lu_bind_function(state, state->int_class, "gt",
                     lu_native_function_new(state, lu_int_gt));
    lu_bind_function(state, state->int_class, "gte",
                     lu_native_function_new(state, lu_int_gte));
    lu_bind_function(state, state->int_class, "eq",
                     lu_native_function_new(state, lu_int_eq));
    lu_bind_function(state, state->int_class, "neq",
                     lu_native_function_new(state, lu_int_neq));

    lu_bind_function(state, state->int_class, "to_str",
                     lu_native_function_new(state, lu_int_to_str));

    lu_bind_function(state, state->int_class, "negate",
                     lu_native_function_new(state, lu_int_negate));

    lu_bind_function(state, state->int_class, "lnot",
                     lu_native_function_new(state, lu_int_lnot));
}
