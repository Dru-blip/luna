#include "luna.h"

LU_NATIVE_FN(Object_to_string);
LU_NATIVE_FN(Object_has_property);
LU_NATIVE_FN(Object_get_proto);

struct lu_object* lu_object_prototype_new(struct lu_istate* state);
