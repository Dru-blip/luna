#include "objects/object_prototype.h"

#include "bytecode/interpreter.h"
#include "bytecode/vm.h"
#include "luna.h"
#include "string_interner.h"
#include "value.h"

LU_NATIVE_FN(Object_to_string) {
    return lu_value_object(lu_intern_string(vm->istate, "Object"));
}

LU_NATIVE_FN(Object_has_property) {
    if (argc == 0) LU_RETURN_NONE();
    struct lu_value key_val = args[0];
    if (!lu_is_string(key_val)) {
        lu_raise_error(vm->istate,
                       lu_string_new(vm->istate, "Key must be a string"),
                       &lu_vm_current_ip_span(vm));
    }
    struct lu_object* curr = self;
    while (curr) {
        if (lu_property_map_has(&curr->properties, lu_as_string(key_val)))
            LU_RETURN_BOOL(true);
        curr = curr->prototype;
    }
    LU_RETURN_BOOL(false);
}

LU_NATIVE_FN(Object_get_proto) {
    return self->prototype ? lu_value_object(self->prototype) : lu_value_none();
}

struct lu_object* lu_object_prototype_new(struct lu_istate* state) {
    struct lu_object* obj = lu_object_new(state);
    obj->prototype = nullptr;

    lu_register_native_fn(state, obj, "toString", Object_to_string, 0);
    lu_register_native_fn(state, obj, "hasProperty", Object_has_property, 1);
    lu_register_native_fn(state, obj, "proto", Object_get_proto, 0);

    lu_obj_set(obj, lu_intern_string(state, "prototype"), lu_value_none());
    return obj;
}
