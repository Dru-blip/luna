#pragma once
#include "ir.h"
#include "value.h"
#include "vm.h"

struct lu_istate {
    struct heap* heap;
    struct lu_object* global_object;
    struct lu_object* module_cache;
    struct arena args_buffer;
    struct string_interner string_pool;
    struct lu_object* error;
    struct span error_location;
    struct lu_module* running_module;
    struct lu_module* main_module;
    struct lu_vm* vm;
    struct generator* ir_generator;
};

struct lu_istate* lu_istate_new();
void lu_istate_destroy(struct lu_istate* state);
struct lu_value lu_run_program(struct lu_istate* state, const char* filepath);

struct lu_value lu_run_executable(struct lu_istate* state,
                                  struct executable* executable);
