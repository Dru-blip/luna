#include "eval.h"
#include "string_interner.h"
#include "value.h"
#include <stdint.h>
#include <stdio.h>

#define lu_define_native(state, name_str, func, pc)                            \
    do {                                                                       \
        struct lu_string *fname = lu_intern_string(state, (char *)(name_str)); \
        struct lu_function *fobj =                                             \
            lu_native_function_new(state, fname, func, pc);                    \
        lu_obj_set((state)->global_object, fname,                              \
                   lu_value_object((struct lu_object *)fobj));                 \
    } while (0)

struct lu_value print_func(struct lu_istate *state, struct argument *args) {
    printf("%ld\n", args[0].value.integer);
    return lu_value_none();
}

struct lu_value raise_func(struct lu_istate *state, struct argument *args) {
    lu_raise_error(state, lu_string_new(state, "raised error"),
                   &state->context_stack->call_stack->call_location);
    return lu_value_none();
}

void lu_init_global_object(struct lu_istate *state) {
    lu_define_native(state, "print", print_func, UINT8_MAX);
    lu_define_native(state, "raise", raise_func, 0);
}
