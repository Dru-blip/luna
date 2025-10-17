#include "runtime/istate.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "parser/parser.h"
#include "runtime/eval.h"
#include "runtime/heap.h"
#include "runtime/object.h"
#include "runtime/objects/boolean.h"
#include "runtime/objects/integer.h"
#include "runtime/objects/strobj.h"
#include "stb_ds.h"
#include "strings/interner.h"

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

static void init_builtin_type_objects(lu_istate_t* state) {
    lu_type_t* str_type_obj = lu_string_type_object_new(state);
    str_type_obj->name_strobj->type = str_type_obj;
    arrput(state->type_registry, str_type_obj);

    lu_type_t* int_type_obj = lu_integer_type_object_new(state);
    arrput(state->type_registry, int_type_obj);

    lu_type_t* bool_type_obj = lu_bool_type_object_new(state);
    arrput(state->type_registry, bool_type_obj);
}

lu_istate_t* lu_istate_new() {
    lu_istate_t* state = malloc(sizeof(lu_istate_t));
    // TODO:
    //  arena_init(&state->strings);
    state->heap = heap_create(state);
    state->string_pool = lu_string_interner_init(state->heap);

    state->type_registry = nullptr;
    init_builtin_type_objects(state);

    state->false_obj = (lu_object_t*)lu_new_bool(state, false);
    state->true_obj = (lu_object_t*)lu_new_bool(state, true);

    return state;
}

void lu_istate_destroy(lu_istate_t* state) {
    // TODO: destroy string pool and other arenas
    // collect_garbage(state->heap);
    heap_destroy(state->heap);
    free(state);
}

static execution_context_t* create_execution_context(
    lu_istate_t* state, execution_context_t* prev) {
    execution_context_t* ctx = malloc(sizeof(execution_context_t));
    ctx->call_stack = nullptr;
    ctx->prev = prev;
    ctx->scope = new_scope(state, nullptr);
    return ctx;
}

static void delete_execution_context(lu_istate_t* state) {
    execution_context_t* ctx = state->context_stack;
    state->context_stack = ctx->prev;
    free(ctx->scope);
    free(ctx);
}

lu_object_t* lu_run_program(lu_istate_t* state, const char* filepath) {
    char* source = read_file(filepath);
    ast_program_t program = parse_program(filepath, source);

    // slmodule_t* new_module = slmodule_new(state->heap, &program);

    state->context_stack =
        create_execution_context(state, state->context_stack);
    state->context_stack->filepath = filepath;
    state->context_stack->program = program;
    call_frame_t* frame = push_call_frame(state->context_stack);
    lu_eval_program(state);
    frame = pop_call_frame(state->context_stack);
    lu_object_t* result = frame->return_value;
    delete_execution_context(state);
    free(frame);
    // new_module->exported_object = result;
    // state->module_cache->vtable->set(state->module_cache, filepath,
    //                                  slvalue_from_obj((slobject_t*)new_module));
    return result;
}
