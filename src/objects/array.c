#include "objects/array.h"

#include <asm-generic/errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bytecode/interpreter.h"
#include "bytecode/vm.h"
#include "luna.h"
#include "strbuf.h"
#include "string_interner.h"
#include "value.h"

LU_NATIVE_FN(Array_push) {
    struct lu_array* arr = lu_cast(struct lu_array, self);

    for (int i = 0; i < argc; i++) {
        lu_array_push(arr, args[i]);
    }

    return lu_value_object(self);
}

LU_NATIVE_FN(Array_pop) {
    struct lu_array* arr = lu_cast(struct lu_array, self);

    if (arr->size == 0) {
        return lu_value_none();
    }

    return arr->elements[arr->size--];
}

LU_NATIVE_FN(Array_insert) {
    struct lu_array* arr = lu_cast(struct lu_array, self);

    int64_t index;
    struct lu_value val;
    LU_TRY_UNPACK_ARGS(vm, "i?", argc, args, &index, &val);

    if (index < 0 || index >= lu_array_length(arr)) {
        char buffer[256];
        snprintf(
            buffer, sizeof(buffer),
            "bad argument #%d (index %ld) out of bounds (array length %ld)", 0,
            index, lu_array_length(arr));
        lu_raise_error(vm->istate, lu_string_new(vm->istate, buffer));
        return lu_value_none();
    }

    if (arr->size + 1 >= arr->capacity) {
        arr->capacity *= 2;
        arr->elements =
            realloc(arr->elements, arr->capacity * sizeof(struct lu_value));
    }

    for (uint32_t i = arr->size - 1; i >= index; --i) {
        arr->elements[i + 1] = arr->elements[i];
    }

    arr->elements[index] = val;
    arr->size++;

    return lu_value_object(self);
}

LU_NATIVE_FN(Array_remove) {
    struct lu_array* arr = lu_cast(struct lu_array, self);

    int64_t index;
    LU_TRY_UNPACK_INT(vm, args, 0, &index);

    if (index < 0 || index >= arr->size) {
        char buffer[256];
        snprintf(
            buffer, sizeof(buffer),
            "bad argument #%d (index %ld) out of bounds (array length %ld)", 0,
            index, lu_array_length(arr));
        lu_raise_error(vm->istate, lu_string_new(vm->istate, buffer));
    }

    for (uint32_t i = index; i < arr->size - 1; ++i) {
        arr->elements[i] = arr->elements[i + 1];
    }

    arr->size--;
    return lu_value_object(self);
}

LU_NATIVE_FN(Array_clear) {
    struct lu_array* arr = lu_cast(struct lu_array, self);

    arr->size = 0;

    return lu_value_object(self);
}

LU_NATIVE_FN(Array_to_string) {
    struct lu_array* arr = lu_cast(struct lu_array, self);

    struct strbuf sb;
    strbuf_init_dynamic(&sb, 256);

    strbuf_append(&sb, "[");
    for (size_t i = 0; i < arr->size; ++i) {
        struct lu_value elem = arr->elements[i];
        switch (elem.type) {
            case VALUE_NONE: {
                strbuf_append(&sb, "");
                break;
            }
            case VALUE_BOOL: {
                strbuf_append(&sb, elem.integer ? "true" : "false");
                break;
            }
            case VALUE_INTEGER: {
                strbuf_appendf(&sb, "%lld", elem.integer);
                break;
            }
            case VALUE_OBJECT: {
                struct lu_value fn =
                    lu_obj_get(lu_as_object(elem),
                               lu_intern_string(vm->istate, "toString"));
                if (lu_is_undefined(fn) || !lu_is_function(fn)) {
                    strbuf_append(&sb, "[Object]");
                    break;
                }
                struct lu_function* func = lu_as_function(fn);
                struct lu_value str_val =
                    lu_call(vm, lu_as_object(elem), func, nullptr, 0, false);
                strbuf_appendf(&sb, "%s",
                               lu_string_get_cstring(lu_as_string(str_val)));
                break;
            }
            default: {
                break;
            }
        }
        if (i < arr->size - 1) {
            strbuf_append(&sb, ", ");
        }
    }
    strbuf_append(&sb, "]");

    return lu_value_object(lu_string_new(vm->istate, sb.buf));
}

LU_NATIVE_FN(Array_iterator_next) {
    struct lu_value* index_val =
        lu_obj_get_ref(self, lu_intern_string(vm->istate, "index"));
    struct lu_array* arr =
        lu_as_array(lu_obj_get(self, lu_intern_string(vm->istate, "data")));
    struct lu_object* obj = lu_object_new(vm->istate);

    bool done = index_val->integer >= arr->size;
    struct lu_value value = lu_value_none();
    if (!done) {
        value = lu_array_get(arr, index_val->integer++);
    }

    lu_obj_set(obj, lu_intern_string(vm->istate, "done"), lu_value_bool(done));
    lu_obj_set(obj, lu_intern_string(vm->istate, "value"), value);

    LU_RETURN_OBJ(obj);
}

LU_NATIVE_FN(Array_iterator) {
    struct lu_object* obj = lu_object_new(vm->istate);

    lu_obj_set(obj, lu_intern_string(vm->istate, "data"),
               lu_value_object(self));
    lu_obj_set(obj, lu_intern_string(vm->istate, "index"), lu_value_int(0));

    lu_register_native_fn(vm->istate, obj, "next", Array_iterator_next, 0);

    LU_RETURN_OBJ(obj);
}

struct lu_object* lu_array_prototype_new(struct lu_istate* state) {
    struct lu_object* obj = lu_object_new(state);

    lu_register_native_fn(state, obj, "push", Array_push, UINT8_MAX);
    lu_register_native_fn(state, obj, "pop", Array_pop, 0);
    lu_register_native_fn(state, obj, "insert", Array_insert, 2);
    lu_register_native_fn(state, obj, "remove", Array_remove, 1);
    lu_register_native_fn(state, obj, "clear", Array_clear, 0);
    lu_register_native_fn(state, obj, "iterator", Array_iterator, 0);
    lu_register_native_fn(state, obj, "toString", Array_to_string, 0);
    return obj;
}
