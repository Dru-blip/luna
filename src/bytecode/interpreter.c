#include "bytecode/interpreter.h"

#include <stdio.h>

#include "ast.h"
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
    arena_init(&state->args_buffer);
    return state;
}

void lu_istate_destroy(struct lu_istate* state) {
    string_interner_destroy(&state->string_pool);
    heap_destroy(state->heap);
    free(state);
}

struct lu_value lu_run_program(struct lu_istate* state, const char* filepath) {
    const char* source = read_file(filepath);
    struct ast_program program = parse_program(filepath, source);
    struct exectuable* executable = generator_generate(program);
    return lu_value_undefined();
}
