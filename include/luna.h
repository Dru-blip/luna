#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "bytecode/interpreter.h"
#include "value.h"

typedef struct lu_value lu_value;

#define LU_EXPORT __attribute__((visibility("default")))
#define ALWAYS_INLINE __attribute__((always_inline))

#define LU_NATIVE_FN(NAME)                                                   \
    LU_EXPORT struct lu_value NAME(struct lu_vm* vm, struct lu_object* self, \
                                   struct lu_value* args, uint8_t argc)

#define LU_RETURN_NONE() return ((lu_value){VALUE_NONE})
#define LU_RETURN_UNDEF() return ((lu_value){VALUE_UNDEFINED})
#define LU_RETURN_INT(x) \
    return ((lu_value){.type = VALUE_INTEGER, .integer = (x)})
#define LU_RETURN_BOOL(x) \
    return ((lu_value){.type = VALUE_BOOL, .integer = (x)})
#define LU_RETURN_OBJ(x) \
    return ((lu_value){.type = VALUE_OBJECT, .object = (x)})

#define LU_ARG_GET(args, index) (args[index])

#define LU_UNPACK_INT(args, idx) ((args)[(idx)].integer)
#define LU_UNPACK_OBJ(args, idx) ((args)[(idx)].obj)
#define LU_UNPACK_VAL(args, idx) ((args)[(idx)])

#define LU_TRY_UNPACK_INT(vm, args, idx, dest)                                 \
    do {                                                                       \
        if (!lu_is_int((args)[(idx)])) {                                       \
            const char* got_type_str_name =                                    \
                lu_value_get_type_name((args)[(idx)]);                         \
            const char* expected_type_str_name = "integer";                    \
            char buffer[256];                                                  \
            snprintf(buffer, sizeof(buffer),                                   \
                     "bad argument #%d: expected '%s', got '%s'", (idx),       \
                     expected_type_str_name, got_type_str_name);               \
            lu_raise_error((vm)->istate, lu_string_new((vm)->istate, buffer)); \
            return lu_value_none();                                            \
        }                                                                      \
        *(int64_t*)(dest) = (args)[(idx)].integer;                             \
    } while (0)

#define LU_TRY_UNPACK_OBJ(vm, args, idx, dest)                                 \
    do {                                                                       \
        if (!lu_is_obj((args)[(idx)])) {                                       \
            const char* got_type_str_name =                                    \
                lu_value_get_type_name((args)[(idx)]);                         \
            const char* expected_type_str_name = "object";                     \
            char buffer[256];                                                  \
            snprintf(buffer, sizeof(buffer),                                   \
                     "bad argument #%d: expected '%s', got '%s'", (idx),       \
                     expected_type_str_name, got_type_str_name);               \
            lu_raise_error((vm)->istate, lu_string_new((vm)->istate, buffer)); \
            return lu_value_none();                                            \
        }                                                                      \
        *(struct lu_obj**)(dest) = (args)[(idx)].object;                       \
    } while (0)

#define LU_TRY_UNPACK_STR(vm, args, idx, dest)                                 \
    do {                                                                       \
        if (!lu_is_string((args)[(idx)])) {                                    \
            const char* got_type_str_name =                                    \
                lu_value_get_type_name((args)[(idx)]);                         \
            const char* expected_type_str_name = "string";                     \
            char buffer[256];                                                  \
            snprintf(buffer, sizeof(buffer),                                   \
                     "bad argument #%d: expected '%s', got '%s'", (idx),       \
                     expected_type_str_name, got_type_str_name);               \
            lu_raise_error((vm)->istate, lu_string_new((vm)->istate, buffer)); \
            return lu_value_none();                                            \
        }                                                                      \
        *(struct lu_string**)(dest) = lu_as_string((args)[(idx)]);             \
    } while (0)

#define LU_TRY_UNPACK_ARGS(vm, typespec, argc, args, ...)                  \
    do {                                                                   \
        uint8_t __i = 0;                                                   \
        const char* __t = (typespec);                                      \
        void* __args[] = {__VA_ARGS__};                                    \
        for (__i = 0; __i < argc; __i++) {                                 \
            switch (__t[__i]) {                                            \
                case 'i': {                                                \
                    if (!lu_is_int(args[__i])) {                           \
                        goto arg_raise_type_error;                         \
                    }                                                      \
                    *((int64_t*)(__args[__i])) = args[__i].integer;        \
                    break;                                                 \
                }                                                          \
                case 's': {                                                \
                    if (!lu_is_string(args[__i])) {                        \
                        goto arg_raise_type_error;                         \
                    }                                                      \
                    *((struct lu_string**)(__args[__i])) =                 \
                        lu_as_string(args[__i]);                           \
                    break;                                                 \
                }                                                          \
                case '?': {                                                \
                    *((lu_value*)(__args[__i])) = args[__i];               \
                    break;                                                 \
                }                                                          \
            }                                                              \
        }                                                                  \
        goto arg_type_check_complete;                                      \
    arg_raise_type_error:                                                  \
        const char* got_type_str_name = lu_value_get_type_name(args[__i]); \
        const char* expected_type_str_name =                               \
            get_type_name_from_mnemonics(__t[__i]);                        \
        char buffer[256];                                                  \
        snprintf(buffer, sizeof(buffer),                                   \
                 "bad argument #%d : expected '%s' got '%s'", __i,         \
                 expected_type_str_name, got_type_str_name);               \
        lu_raise_error(vm->istate, lu_string_new(vm->istate, buffer));     \
        return lu_value_none();                                            \
    arg_type_check_complete:                                               \
    } while (0);

ALWAYS_INLINE static inline const char* get_type_name_from_mnemonics(char c) {
    switch (c) {
        case 'i':
            return "integer";
        case 'b':
            return "bool";
        case 'o':
            return "object";
        default:
            return "unknown";
    }
}

ALWAYS_INLINE static inline void lu_register_native_fn(struct lu_istate* state,
                                                       struct lu_object* obj,
                                                       const char* name_str,
                                                       native_func_t func,
                                                       int pc) {
    struct lu_string* fname = lu_intern_string(state, (char*)name_str);
    struct lu_function* fobj = lu_native_function_new(state, fname, func, pc);
    lu_obj_set(obj, fname, lu_value_object((struct lu_object*)fobj));
}
