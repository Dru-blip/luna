#include "luna.h"

LU_NATIVE_FN(Array_push);
LU_NATIVE_FN(Array_pop);
LU_NATIVE_FN(Array_insert);
LU_NATIVE_FN(Array_remove);
LU_NATIVE_FN(Array_clear);
LU_NATIVE_FN(Array_to_string);
LU_NATIVE_FN(Array_iterator_next);
LU_NATIVE_FN(Array_iterator);

struct lu_object* lu_array_prototype_new(struct lu_istate* state);
