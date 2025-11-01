#include "objects/process.h"

#include <errno.h>
#include <linux/limits.h>
#include <unistd.h>

#include "luna.h"
#include "value.h"

LU_NATIVE_FN(Process_cwd) {
    char buf[PATH_MAX];
    if (getcwd(buf, PATH_MAX)) {
        return lu_value_object(lu_string_new(vm->istate, buf));
    }

    const char* err = strerror(errno);

    lu_raise_error(vm->istate, lu_string_new(vm->istate, err));
    return lu_value_none();
}

struct lu_object* lu_process_object_new(struct lu_istate* state,
                                        struct lu_array* args) {
    //
    struct lu_object* obj = lu_object_new(state);

    lu_obj_set(obj, lu_intern_string(state, "argv"), lu_value_object(args));
    lu_register_native_fn(state, obj, "cwd", Process_cwd, 0);

    return obj;
}
