#include "runtime/eval.h"

#include "operator.h"
#include "runtime/heap.h"
#include "runtime/istate.h"
#include "runtime/object.h"
#include "runtime/objects/integer.h"
#include "stb_ds.h"

call_frame_t* push_call_frame(execution_context_t* ctx) {
    call_frame_t* frame = calloc(1, sizeof(call_frame_t));
    frame->parent = ctx->call_stack;
    ctx->call_stack = frame;
    frame->return_value = nullptr;
    ctx->frame_count++;
    return frame;
}

call_frame_t* pop_call_frame(execution_context_t* ctx) {
    call_frame_t* frame = ctx->call_stack;
    ctx->call_stack = frame->parent;
    ctx->frame_count--;
    return frame;
}

scope_t* new_scope(heap_t* heap, scope_t* parent) {
    scope_t* scope = calloc(1, sizeof(scope_t));
    scope->values = heap_allocate_object(heap, sizeof(lu_object_t));
    scope->depth = parent ? parent->depth + 1 : 0;
    scope->parent = parent;
    return scope;
}

scope_t* new_scope_with(heap_t* heap, scope_t* parent, lu_object_t* values) {
    scope_t* scope = calloc(1, sizeof(scope_t));
    scope->values = values;
    scope->parent = parent;

    return scope;
}

static void begin_scope(lu_istate_t* state) {
    state->context_stack->scope =
        new_scope(state->heap, state->context_stack->scope);
}

static void end_scope(lu_istate_t* state) {
    scope_t* scope = state->context_stack->scope;
    state->context_stack->scope = scope->parent;
    state->context_stack->scope->depth--;
    free(scope);
}

static lu_object_t* lu_binop(lu_istate_t* state, lu_object_t* a, lu_object_t* b,
                             binary_op_t op_slot) {
    lu_object_t* res = nullptr;

    if (a->type->binop_slots[op_slot]) {
        res = a->type->binop_slots[op_slot](state, a, b);
        if (state->op_result != op_result_not_implemented) {
            return res;
        }
    }

    if (b->type->binop_slots[op_slot]) {
        res = b->type->binop_slots[op_slot](state, a, b);
        if (state->op_result != op_result_not_implemented) {
            return res;
        }
    }

    return res;
}

static lu_object_t* eval_expr(lu_istate_t* state, ast_node_t* expr) {
    switch (expr->kind) {
        case ast_node_kind_int: {
            return (lu_object_t*)lu_new_integer(state, expr->data.int_val);
        }
        case ast_node_kind_binop: {
            lu_object_t* lhs = eval_expr(state, expr->data.binop.lhs);
            lu_object_t* rhs = eval_expr(state, expr->data.binop.rhs);

            // Check for error
            return lu_binop(state, lhs, rhs, expr->data.binop.op);
        }
        default: {
            break;
        }
    }
}

static signal_kind_t eval_stmt(lu_istate_t* state, ast_node_t* stmt) {
    switch (stmt->kind) {
        case ast_node_kind_return: {
            state->context_stack->call_stack->return_value =
                eval_expr(state, stmt->data.node);
            return signal_return;
        }
        default: {
            break;
        }
    }
    return signal_none;
}

static signal_kind_t eval_stmts(lu_istate_t* state, ast_node_t** stmts) {
    const uint32_t nstmts = arrlen(stmts);
    for (uint32_t i = 0; i < nstmts; i++) {
        signal_kind_t sig = eval_stmt(state, stmts[i]);
        if (sig != signal_none) return sig;
    }

    return signal_none;
}

void lu_eval_program(lu_istate_t* state) {
    eval_stmts(state, state->context_stack->program.nodes);
}
