#include <dirent.h>
#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "eval.h"
#include "luna.h"
#include "strbuf.h"
#include "string_interner.h"
#include "value.h"

#define lu_define_native(obj, name_str, func, pc)                             \
    do {                                                                      \
        struct lu_string* fname = lu_intern_string(state, (char*)(name_str)); \
        struct lu_function* fobj =                                            \
            lu_native_function_new(state, fname, func, pc);                   \
        lu_obj_set((obj), fname, lu_value_object((struct lu_object*)fobj));   \
    } while (0)

struct lu_value print_func(struct lu_istate* state, struct argument* args) {
    size_t arg_count = LU_ARG_COUNT(state);
    for (size_t i = 0; i < arg_count; i++) {
        switch (args[i].value.type) {
            case VALUE_BOOL: {
                printf("%s ", args[i].value.integer ? "true" : "false");
                break;
            }
            case VALUE_INTEGER: {
                printf("%ld ", args[i].value.integer);
                break;
            }
            case VALUE_NONE: {
                printf("none ");
                break;
            }
            case VALUE_OBJECT: {
                if (lu_is_string(args[i].value)) {
                    printf("%s ",
                           lu_string_get_cstring(lu_as_string(args[i].value)));
                    break;
                }
                printf("Object<%p> ", lu_as_object(args[i].value));
                break;
            }
        }
    }
    printf("\n");

    return lu_value_none();
}

struct lu_value raise_func(struct lu_istate* state, struct argument* args) {
    lu_raise_error(state, lu_string_new(state, "raised error"),
                   &state->context_stack->call_stack->call_location);
    return lu_value_none();
}

struct lu_value len(struct lu_istate* state, struct argument* args) {
    struct lu_value arg = args[0].value;
    switch (arg.type) {
        case VALUE_BOOL: {
            return lu_value_int(1);
        }
        case VALUE_INTEGER: {
            // TODO: implement digit count
            return arg;
        }
        case VALUE_OBJECT: {
            if (lu_is_string(arg)) {
                return lu_value_int(lu_as_string(arg)->length);
            }
            if (lu_is_array(arg)) {
                return lu_value_int(lu_as_array(arg)->size);
            }
            return lu_value_int(lu_obj_size(lu_as_object(arg)));
        }
        default: {
            return lu_value_int(0);
        }
    }
}

struct lu_value import_module(struct lu_istate* state, struct argument* args) {
    // TODO: handle package format (foo.bar)
    struct lu_value file_path_value = args[0].value;
    // TODO: add checks for file path
    struct lu_value module =
        lu_obj_get(state->module_cache, lu_as_string(file_path_value));
    if (!lu_is_undefined(module)) {
        return lu_cast(struct lu_module, module.object)->exported;
    }

    struct strbuf sb;
    char err_buffer[256];
    strbuf_init_static(&sb, err_buffer, sizeof(err_buffer));

    char path[PATH_MAX];
    if (getcwd(path, sizeof(path)) == nullptr) {
        // TODO: report error
        return lu_value_undefined();
    }

    DIR* dir;
    struct dirent* file_info;

    if ((dir = opendir(path)) == nullptr) {
        strbuf_appendf(&sb, "failed to read file: '%s'",
                       lu_string_get_cstring(lu_as_string(file_path_value)));
        lu_raise_error(state, lu_string_new(state, err_buffer),
                       &state->context_stack->call_stack->call_location);

        return lu_value_undefined();
    }

    char file_path[PATH_MAX];
    snprintf(file_path, PATH_MAX, "%s.%s",
             lu_string_get_cstring(lu_as_string(file_path_value)), "luna");

    char* path_buffer = nullptr;
    while ((file_info = readdir(dir)) != nullptr) {
        if (strcmp(file_info->d_name, file_path) == 0) {
            path_buffer = malloc(PATH_MAX + 256);
            snprintf(path_buffer, PATH_MAX + 256, "%s/%s", path,
                     file_info->d_name);
            break;
        }
    }

    if (path_buffer == nullptr) {
        closedir(dir);
        strbuf_appendf(&sb, "failed to read file: '%s'", file_path);
        lu_raise_error(state, lu_string_new(state, err_buffer),
                       &state->context_stack->call_stack->call_location);
        return lu_value_undefined();
    }

    struct lu_value result = lu_run_program(state, path_buffer);
    closedir(dir);
    free(path_buffer);

    return result;
}

struct lu_value console_read_int(struct lu_istate* state,
                                 struct argument* args) {
    //
    size_t arg_count = LU_ARG_COUNT(state);
    if (arg_count <= 0) goto read_impl;
    struct lu_value help = args[0].value;
    if (!lu_is_string(help)) {
        lu_raise_error(state,
                       lu_string_new(state, "Expected a string argument"),
                       &state->context_stack->call_stack->call_location);
        return lu_value_none();
    }
    printf("%s", lu_string_get_cstring(lu_as_string(help)));

read_impl:
    int64_t in;
    scanf("%ld", &in);
    return lu_value_int(in);
}

static void lu_init_console_object(struct lu_istate* state) {
    struct lu_object* console_obj = lu_object_new(state);
    lu_define_native(console_obj, "read_int", console_read_int, 1);
    lu_obj_set(state->global_object, lu_intern_string(state, "console"),
               lu_value_object(console_obj));
}

void lu_init_global_object(struct lu_istate* state) {
    lu_define_native(state->global_object, "print", print_func, UINT8_MAX);
    lu_define_native(state->global_object, "raise", raise_func, 0);
    lu_define_native(state->global_object, "import", import_module, 1);
    lu_define_native(state->global_object, "len", len, 1);

    lu_init_console_object(state);
}
