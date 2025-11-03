#include "luna.h"

LU_NATIVE_FN(String_to_string);
LU_NATIVE_FN(String_char_at);
LU_NATIVE_FN(String_substring);
LU_NATIVE_FN(String_index_of);

struct lu_object* lu_string_prototype_new(struct lu_istate* state);
