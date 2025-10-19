#pragma once

#include <stddef.h>

#include "ast.h"
#include "heap.h"
#include "value.h"

enum signal_kind {
    SIGNAL_NONE,
    SIGNAL_BREAK,
    SIGNAL_CONTINUE,
    SIGNAL_RETURN,
    SIGNAL_ERROR,
};

enum op_result_kind {
    OP_RESULT_SUCCESS,
    OP_RESULT_NOT_IMPLEMENTED,
    OP_RESULT_RAISED_ERROR,
};

struct scope {
    struct lu_dict* symbols;
    struct scope* parent;
    size_t depth;
};

struct call_frame {
    struct lu_object* self;
    struct call_frame* parent;
    struct lu_object* return_value;
};

struct execution_context {
    struct scope* scope;
    struct call_frame* call_stack;
    struct execution_context* prev;
    size_t scope_depth;
    struct ast_program program;
    size_t frame_count;
    const char* filepath;
};

struct lu_istate {
    struct lu_klass* base_object;
    struct lu_klass* base_class;
    struct lu_klass* int_class;
    struct lu_klass* str_class;
    struct lu_klass* dict_class;
    struct lu_klass* function_class;
    struct heap* heap;
    struct execution_context* context_stack;
    enum op_result_kind op_result;
};

struct lu_istate* lu_istate_new(void);
void lu_istate_destroy(struct lu_istate* state);

struct scope* new_scope(struct lu_istate* state, struct scope* parent);
struct scope* new_scope_with(struct heap* heap, struct scope* parent,
                             struct lu_dict* symbols);
struct call_frame* push_call_frame(struct execution_context* ctx);
struct call_frame* pop_call_frame(struct execution_context* ctx);

void lu_eval_program(struct lu_istate* state);
struct lu_object* lu_call_function(struct lu_istate* state,
                                   struct lu_object* self);
struct lu_object* lu_run_program(struct lu_istate* state, const char* filepath);
