#include "eval.h"
#include "value.h"

struct lu_value lu_int_add(struct lu_istate* state, struct lu_value* self,
                           struct argument* args) {
    //
    return luvalue_int(self->integer + args->value.integer);
}

void lu_integer_bind_methods(struct lu_istate* state) {
    lu_bind_function(state, state->int_class, "add",
                     lu_native_function_new(state, lu_int_add));
}
