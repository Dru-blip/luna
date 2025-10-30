#include "bytecode/interpreter.h"

#include <stdio.h>

#include "ast.h"
#include "bytecode/ir.h"
#include "bytecode/vm.h"
#include "heap.h"
#include "parser.h"
#include "value.h"

static char* read_file(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("Error: could not open file %s\n", filename);
        exit(EXIT_FAILURE);
    }
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    rewind(f);
    char* buffer = malloc(len + 1);
    buffer[len] = '\0';
    fread(buffer, 1, len, f);
    fclose(f);
    return buffer;
}

struct lu_istate* lu_istate_new() {
    struct lu_istate* state = malloc(sizeof(struct lu_istate));
    state->heap = heap_create(state);
    string_interner_init(state);
    state->global_object = lu_object_new(state);
    state->module_cache = lu_object_new(state);
    state->running_module = nullptr;
    state->main_module = nullptr;
    state->vm = lu_vm_new(state);
    state->error = nullptr;
    arena_init(&state->args_buffer);
    lu_init_global_object(state);
    return state;
}

void lu_istate_destroy(struct lu_istate* state) {
    string_interner_destroy(&state->string_pool);
    heap_destroy(state->heap);
    free(state);
}

static void print_value(struct lu_value value) {
    switch (value.type) {
        case VALUE_BOOL: {
            printf("%s ", value.integer ? "true" : "false");
            break;
        }
        case VALUE_INTEGER: {
            printf("%ld ", value.integer);
            break;
        }
        case VALUE_NONE: {
            printf("none ");
            break;
        }
        case VALUE_OBJECT: {
            if (lu_is_string(value)) {
                printf("%s ", lu_string_get_cstring(lu_as_string(value)));
                break;
            }
            printf("Object<%p> ", lu_as_object(value));
            break;
        }
    }
    printf("\n");
}

struct lu_value lu_run_program(struct lu_istate* state, const char* filepath) {
    const char* source = read_file(filepath);
    struct ast_program program = parse_program(filepath, source);
    struct lu_module* module =
        lu_module_new(state, lu_string_new(state, filepath), &program);

    struct lu_module* prev_module = state->running_module;
    state->running_module = module;

    if (!state->main_module) {
        state->main_module = module;
    }

    struct executable* executable = generator_generate(state, program);
    if (state->error) {
        if (state->running_module != state->main_module) {
            state->vm->status = VM_STATUS_HALT;
            return lu_value_undefined();
        }
        struct lu_string* str = lu_as_string(
            lu_obj_get(state->error, lu_intern_string(state, "message")));
        struct lu_string* traceback = lu_as_string(
            lu_obj_get(state->error, lu_intern_string(state, "traceback")));
        printf("Error: %s\n", lu_string_get_cstring(str));
        if (traceback) {
            printf("%s\n", traceback->block->data);
        }
        return lu_value_undefined();
    }
    print_executable(executable);
    lu_obj_set(state->module_cache, module->name, lu_value_object(module));
    struct lu_value result = lu_value_none();
    result = lu_run_executable(state, executable);
    // print_value(result);
    state->running_module = prev_module;

    return result;
    // return lu_value_none();
}
