#include "eval.h"
#include "string_interner.h"
#include "value.h"
#include <stdio.h>

#define lu_define_native(state, name_str, func)                                \
    do {                                                                       \
        struct lu_string *fname = lu_string_new(state, (char *)(name_str));    \
        struct lu_function *fobj = lu_native_function_new(state, fname, func); \
        lu_obj_set((state)->global_object, fname,                              \
                   lu_value_object((struct lu_object *)fobj));                 \
    } while (0)

struct lu_value print_func(struct lu_istate *state, struct argument *args) {
    printf("printing %d \n", 5);
    return lu_value_none();
}

void lu_init_global_object(struct lu_istate *state) {
    lu_define_native(state, "print", print_func);
}
