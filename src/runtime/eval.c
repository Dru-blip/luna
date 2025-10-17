#include "runtime/eval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operator.h"
#include "parser/ast.h"
#include "parser/tokenizer.h"
#include "runtime/heap.h"
#include "runtime/istate.h"
#include "runtime/luerrors.h"
#include "runtime/object.h"
#include "runtime/objects/boolean.h"
#include "runtime/objects/hashmap.h"
#include "runtime/objects/integer.h"
#include "runtime/objects/strobj.h"
#include "stb_ds.h"
#include "strings/interner.h"

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

scope_t* new_scope(lu_istate_t* state, scope_t* parent) {
    scope_t* scope = calloc(1, sizeof(scope_t));
    scope->values = lu_hashmap_new(state);
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
    state->context_stack->scope = new_scope(state, state->context_stack->scope);
}

static void end_scope(lu_istate_t* state) {
    scope_t* scope = state->context_stack->scope;
    state->context_stack->scope = scope->parent;
    state->context_stack->scope->depth--;
    free(scope);
}

static inline void set_variable(lu_istate_t* state, lu_object_t* name,
                                lu_object_t* value) {
    scope_t* scope = state->context_stack->scope;
    lu_hashmap_put(state, (lu_hashmap_t*)scope->values, name, value);
}

static inline lu_object_t* get_variable(lu_istate_t* state, lu_object_t* name) {
    scope_t* scope = state->context_stack->scope;
    while (scope) {
        lu_object_t* val = lu_hashmap_get((lu_hashmap_t*)scope->values, name);
        if (val) return val;
        scope = scope->parent;
    }
    return nullptr;
}

static signal_kind_t eval_stmts(lu_istate_t* state, ast_node_t** stmts);

static inline bool is_truthy(lu_object_t* value) {
    if (value->type == Integer_type) {
        return ((lu_integer_t*)value)->value != 0;
    }
    if (value->type == Bool_type) {
        return ((lu_bool_t*)value)->value;
    }
    if (value->type == Str_type) {
        return ((lu_string_t*)value)->length != 0;
    }
    return false;
}

static lu_object_t* lu_binop(lu_istate_t* state, lu_object_t* a, lu_object_t* b,
                             binary_op_t op_slot) {
    /**
     * Performs a binary operation between two Lu objects.
     *
     * function dispatches the given binary operation (`op_slot`) between two
     * operands `a` and `b` based on their types operator slot implementations.
     *
     * It currently only supports operations between objects of the same type,
     * or if both operand types define their own implementation for the
     * operation. There is no automatic type coercion or inheritance-based
     * dispatch (e.g., between a parent and child type). This behavior is
     * planned for future implementation.
     *
     * @note The function checks whether the operation is implemented on the
     * left-hand side (`a->type`) first. If it returns a result and the
     * interpreter does not mark the operation as `op_result_not_implemented`,
     * that result is returned immediately.
     *
     * If the operation was not handled by `a->type`, it then checks
     * `b->type->binop_slots[op_slot]` (the right-hand side type). If the
     * right-hand side type implements the operation and returns a valid result,
     * it is returned.
     *
     * TODO:
     * - Implement type coercion to handle mixed-type operations.
     * - Implement subtype slot dispatch so child types can delegate to parent
     *   type operators.
     */
    lu_object_t* res = nullptr;

    if (a->type->binop_slots[op_slot]) {
        res = a->type->binop_slots[op_slot](state, a, b);
        // Find a better way to check and propagate errors instead of if-checks
        // which could slow down the eval loop.
        if (state->op_result == op_result_raised_error) {
            return nullptr;
        }
        if (state->op_result != op_result_not_implemented) {
            return res;
        }
    }

    if (a->type != b->type && b->type->binop_slots[op_slot]) {
        res = b->type->binop_slots[op_slot](state, a, b);
        if (state->op_result == op_result_raised_error) {
            return nullptr;
        }
        if (state->op_result != op_result_not_implemented &&
            state->op_result != op_result_raised_error) {
            return res;
        }
    }

    state->op_result = op_result_raised_error;

    size_t needed =
        snprintf(nullptr, 0, "unsupported operand type(s) for %s : %s and %s",
                 binary_op_labels[op_slot], a->type->name, b->type->name) +
        1;
    char* buffer = malloc(needed);

    snprintf(buffer, needed, "unsupported operand type(s) for %s : %s and %s",
             binary_op_labels[op_slot], a->type->name, b->type->name);

    buffer[needed] = '\0';

    lu_error_t* error = lu_error_from_str(state, buffer);
    state->error = error;
    free(buffer);

    return res;
}

lu_string_t* intern_identifier(lu_istate_t* state, span_t* span) {
    const size_t len = span->end - span->start;
    char* buffer = malloc(len);
    memcpy(buffer, state->context_stack->program.source + span->start, len);
    lu_string_t* str = lu_intern_string(state->string_pool, buffer, len);
    free(buffer);
    return str;
}

static lu_object_t* eval_expr(lu_istate_t* state, ast_node_t* expr) {
    switch (expr->kind) {
        case ast_node_kind_int: {
            return (lu_object_t*)lu_new_integer(state, expr->data.int_val);
        }
        case ast_node_kind_bool: {
            return expr->data.int_val ? state->true_obj : state->false_obj;
        }
        case ast_node_kind_identifier: {
            lu_object_t* obj = get_variable(
                state, (lu_object_t*)intern_identifier(state, &expr->span));
            if (!obj) {
                state->error =
                    lu_error_from_str(state, "undeclared identifier");
                ((lu_error_t*)state->error)->span = expr->span;
                ((lu_error_t*)state->error)->line = expr->span.line;
                state->op_result = op_result_raised_error;
            }
            return obj;
        }
        case ast_node_kind_unop: {
            lu_object_t* argument = eval_expr(state, expr->data.unop.argument);
            unary_func f = argument->type->unop_slots[expr->data.unop.op];
            if (!f) {
                state->error = lu_error_from_str(
                    state, "cannot perform unary () on types");
                ((lu_error_t*)state->error)->span = expr->span;
                ((lu_error_t*)state->error)->line = expr->span.line;
                state->op_result = op_result_raised_error;
                return nullptr;
            }
            return f(state, argument);
        }
        case ast_node_kind_binop: {
            lu_object_t* lhs = eval_expr(state, expr->data.binop.lhs);
            // Find a better way to check and propagate errors instead of
            // if-checks which could slow down the eval loop.
            if (state->op_result == op_result_raised_error) {
                return lhs;
            }
            lu_object_t* rhs = eval_expr(state, expr->data.binop.rhs);
            if (state->op_result == op_result_raised_error) {
                return rhs;
            }
            // Check for error
            lu_object_t* res = lu_binop(state, lhs, rhs, expr->data.binop.op);

            if (state->error) {
                ((lu_error_t*)state->error)->span = expr->span;
                ((lu_error_t*)state->error)->line = expr->span.line;
            }

            return res;
        }
        case ast_node_kind_assign: {
            lu_object_t* value = eval_expr(state, expr->data.binop.rhs);
            lu_string_t* name =
                intern_identifier(state, &expr->data.binop.lhs->span);
            set_variable(state, (lu_object_t*)name, value);
            return value;
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
            if (state->op_result == op_result_raised_error) {
                return signal_error;
            }
            return signal_return;
        }
        case ast_node_kind_block: {
            begin_scope(state);
            signal_kind_t sig = eval_stmts(state, stmt->data.list);
            end_scope(state);
            return sig;
        }
        case ast_node_kind_if_stmt: {
            lu_object_t* cond = eval_expr(state, stmt->data.if_stmt.test);
            if (is_truthy(cond)) {
                return eval_stmt(state, stmt->data.if_stmt.consequent);
            }
            if (stmt->data.if_stmt.alternate) {
                return eval_stmt(state, stmt->data.if_stmt.alternate);
            }
            break;
        }
        case ast_node_kind_expr_stmt: {
            lu_object_t* res = eval_expr(state, stmt->data.node);
            if (state->op_result == op_result_raised_error) {
                return signal_error;
            }
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
    signal_kind_t signal =
        eval_stmts(state, state->context_stack->program.nodes);
    if (signal == signal_error) {
        lu_error_t* err = (lu_error_t*)state->error;
        lu_print_error(err);
        // printf("Error: %s at line %ld\n", err->message, err->line);
    }
}
