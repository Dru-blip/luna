#pragma once

#include <stddef.h>

#include "arena.h"
#include "parser/ast.h"
#include "runtime/heap.h"
#include "runtime/object.h"

typedef enum signal_kind {
    signal_none,
    signal_break,
    signal_continue,
    signal_return,
    signal_error,
} signal_kind_t;

typedef enum op_result_kind {
    op_result_success,
    op_result_not_implemented,
    op_result_raised_error,
} op_result_kind_t;

typedef struct scope {
    lu_object_t* values;
    struct scope* parent;
    size_t depth;
} scope_t;

typedef struct call_frame {
    lu_object_t* self;
    struct call_frame* parent;
    lu_object_t* return_value;
} call_frame_t;

typedef struct execution_context {
    scope_t* scope;
    call_frame_t* call_stack;
    struct execution_context* prev;
    size_t scope_depth;
    ast_program_t program;
    size_t frame_count;
    const char* filepath;
} execution_context_t;

typedef struct lu_istate {
    lu_object_t* builtins;
    lu_type_t** type_registry;
    lu_object_t* true_obj;
    lu_object_t* false_obj;
    lu_object_t* error;
    heap_t* heap;
    lu_object_t* module_cache;
    execution_context_t* context_stack;
    op_result_kind_t op_result;
    struct string_interner* string_pool;
} lu_istate_t;

lu_istate_t* lu_istate_new();
void lu_istate_destroy(lu_istate_t* state);
scope_t* new_scope(heap_t* heap, scope_t* parent);
scope_t* new_scope_with(heap_t* heap, scope_t* parent, lu_object_t* values);
call_frame_t* push_call_frame(execution_context_t* ctx);
call_frame_t* pop_call_frame(execution_context_t* ctx);
