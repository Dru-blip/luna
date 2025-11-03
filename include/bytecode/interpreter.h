#pragma once
#include "ir.h"
#include "value.h"
#include "vm.h"

struct common_names {
    struct lu_string* Object;
    struct lu_string* Object_Object;
    struct lu_string* Array;
    struct lu_string* True;
    struct lu_string* False;
    struct lu_string* none;

    struct lu_string* toString;

    struct lu_string* message;
    struct lu_string* traceback;

    struct lu_string* data;
    struct lu_string* index;

    struct lu_string* done;
    struct lu_string* value;
    struct lu_string* next;

    struct lu_string* push;
    struct lu_string* pop;
    struct lu_string* insert;
    struct lu_string* remove;
    struct lu_string* clear;
    struct lu_string* iterator;

    struct lu_string* console;
    struct lu_string* print;
    struct lu_string* log;
    struct lu_string* process;

    struct lu_string* import;
    struct lu_string* len;
    struct lu_string* readInt;
    struct lu_string* min;
    struct lu_string* max;
    struct lu_string* pow;
    struct lu_string* abs;
    struct lu_string* Math;

    struct lu_string* hasProperty;
    struct lu_string* proto;
    struct lu_string* prototype;

    struct lu_string* charAt;
    struct lu_string* substring;
    struct lu_string* indexOf;
};

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

    struct lu_object* object_prototype;
    struct lu_object* array_prototype;
    struct lu_object* string_prototype;

    struct common_names names;
};

struct lu_istate* lu_istate_new();
void lu_istate_process_init(struct lu_istate* state, int argc, char* argv[]);
void lu_istate_destroy(struct lu_istate* state);
struct lu_value lu_run_program(struct lu_istate* state, const char* filepath);

struct lu_value lu_run_executable(struct lu_istate* state, struct executable* executable);
struct lu_value lu_call(struct lu_vm* vm,
                        struct lu_object* self,
                        struct lu_function* function,
                        struct lu_value* args,
                        uint8_t argc,
                        bool as_callback);
