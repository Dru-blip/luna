#include "luna.h"
#include "value.h"

LU_NATIVE_FN(Process_cwd);

struct lu_object* lu_process_object_new(struct lu_istate* state,
                                        struct lu_array* args);
