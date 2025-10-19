#include "eval.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "ast.h"
#include "eval.h"
#include "heap.h"
#include "operator.h"
#include "parser.h"
#include "stb_ds.h"
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
    state->context_stack = nullptr;

    lu_init_core_klasses(state);

    return state;
}

void lu_istate_destroy(struct lu_istate* state) {
    heap_destroy(state->heap);
    free(state);
}

static struct execution_context* create_execution_context(
    struct lu_istate* state, struct execution_context* prev) {
    struct execution_context* ctx = malloc(sizeof(struct execution_context));
    ctx->call_stack = nullptr;
    ctx->prev = prev;
    ctx->scope = new_scope(state, nullptr);
    return ctx;
}

static void delete_execution_context(struct lu_istate* state) {
    struct execution_context* ctx = state->context_stack;
    state->context_stack = ctx->prev;
    arrfree(ctx->program.tokens);
    free(ctx->program.source);
    arena_destroy(&ctx->program.allocator);
    free(ctx->scope);
    free(ctx);
}

struct lu_object* lu_run_program(struct lu_istate* state,
                                 const char* filepath) {
    char* source = read_file(filepath);
    struct ast_program program = parse_program(filepath, source);
    state->context_stack =
        create_execution_context(state, state->context_stack);
    state->context_stack->filepath = filepath;
    state->context_stack->program = program;
    struct call_frame* frame = push_call_frame(state->context_stack);
    lu_eval_program(state);
    frame = pop_call_frame(state->context_stack);
    struct lu_object* result = frame->return_value;
    delete_execution_context(state);
    free(frame);
    return result;
}

struct call_frame* push_call_frame(struct execution_context* ctx) {
    struct call_frame* frame = calloc(1, sizeof(struct call_frame));
    frame->parent = ctx->call_stack;
    ctx->call_stack = frame;
    frame->return_value = nullptr;
    ctx->frame_count++;
    return frame;
}

struct call_frame* pop_call_frame(struct execution_context* ctx) {
    struct call_frame* frame = ctx->call_stack;
    ctx->call_stack = frame->parent;
    ctx->frame_count--;
    return frame;
}

struct scope* new_scope(struct lu_istate* state, struct scope* parent) {
    struct scope* scope = calloc(1, sizeof(struct scope));
    scope->symbols = lu_dict_new(state);
    scope->depth = parent ? parent->depth + 1 : 0;
    scope->parent = parent;
    return scope;
}

struct scope* new_scope_with(struct heap* heap, struct scope* parent,
                             struct lu_dict* symbols) {
    struct scope* scope = calloc(1, sizeof(struct scope));
    scope->symbols = symbols;
    scope->parent = parent;
    return scope;
}

static void begin_scope(struct lu_istate* state) {
    state->context_stack->scope = new_scope(state, state->context_stack->scope);
}

static void end_scope(struct lu_istate* state) {
    struct scope* scope = state->context_stack->scope;
    state->context_stack->scope = scope->parent;
    state->context_stack->scope->depth--;
    free(scope);
}

static inline void set_variable(struct lu_istate* state, struct lu_value name,
                                struct lu_value value) {
    struct scope* scope = state->context_stack->scope;
    while (scope) {
        if (lu_dict_get(scope->symbols, name).type != VALUE_UNDEFINED) {
            break;
        }
        scope = scope->parent;
    }
    if (scope) {
        lu_dict_put(state, scope->symbols, name, value);
    } else {
        lu_dict_put(state, state->context_stack->scope->symbols, name, value);
    }
}

static inline struct lu_klass* lu_get_class(struct lu_istate* state,
                                            struct lu_value* value) {
    // TODO: handle remaining cases
    switch (value->type) {
        case VALUE_INTEGER: {
            return state->int_class;
        }
        case VALUE_OBJECT: {
            return value->obj->klass;
        }
    }
    return nullptr;
}

struct lu_string* get_identifier(struct lu_istate* state, struct span* span) {
    const size_t len = span->end - span->start;
    char* buffer = malloc(len + 1);
    memcpy(buffer, state->context_stack->program.source + span->start, len);
    buffer[len] = '\0';
    struct lu_string* str = lu_string_new(state, buffer);
    free(buffer);
    return str;
}

static enum signal_kind eval_stmts(struct lu_istate* state,
                                   struct ast_node** stmts);
static enum signal_kind eval_stmt(struct lu_istate* state,
                                  struct ast_node* stmt);

static struct lu_value eval_expr(struct lu_istate* state,
                                 struct ast_node* expr) {
    switch (expr->kind) {
        case AST_NODE_INT: {
            return LUVALUE_INT(expr->data.int_val);
        }
        case AST_NODE_BOOL: {
            return expr->data.int_val ? LUVALUE_TRUE : LUVALUE_FALSE;
        }
        case AST_NODE_UNOP: {
            struct lu_value argument =
                eval_expr(state, expr->data.unop.argument);

            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return LUVALUE_NULL;
            }

            struct lu_klass* klass = lu_get_class(state, &argument);
            if (!klass) {
                // TODO: handle other klasses
            }

            struct lu_value method_val =
                lu_dict_get(klass->methods,
                            LUVALUE_OBJ((struct lu_object*)lu_string_new(
                                state, unary_op_labels[expr->data.unop.op])));

            if (method_val.type == VALUE_NULL) {
                state->op_result = OP_RESULT_RAISED_ERROR;
                state->exception = lu_error_new_printf(
                    state, "TypeError",
                    "Unsupported operation '%s' on type '%s'",
                    unary_op_labels[expr->data.unop.op], klass->dbg_name);

                return LUVALUE_NULL;
            }

            struct argument arg;
            struct lu_function* method = method_val.obj;
            arg.value = argument;
            return method->native_func(state, method, &arg);
        }
        case AST_NODE_BINOP: {
            struct lu_value lhs = eval_expr(state, expr->data.binop.lhs);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return LUVALUE_NULL;
            }
            struct lu_value rhs = eval_expr(state, expr->data.binop.rhs);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return LUVALUE_NULL;
            }
            struct lu_klass* klass = lu_get_class(state, &lhs);
            struct lu_value method_val =
                lu_dict_get(klass->methods,
                            LUVALUE_OBJ((struct lu_object*)lu_string_new(
                                state, binary_op_labels[expr->data.binop.op])));
            if (method_val.type == VALUE_NULL) {
                state->op_result = OP_RESULT_RAISED_ERROR;
                state->exception = lu_error_new_printf(
                    state, "TypeError",
                    "Unsupported operation '%s' on type '%s'",
                    binary_op_labels[expr->data.binop.op], klass->dbg_name);

                return LUVALUE_NULL;
            }

            struct argument args[2];
            struct lu_function* method = method_val.obj;
            args[0].value = lhs;
            args[1].value = rhs;
            return method->native_func(state, method, args);
        }
        case AST_NODE_ASSIGN: {
            struct lu_value value = eval_expr(state, expr->data.binop.rhs);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return LUVALUE_NULL;
            }
            struct lu_string* name =
                get_identifier(state, &expr->data.binop.lhs->span);
            set_variable(state, LUVALUE_OBJ(name), value);
            return value;
        }
        default: {
            break;
        }
    }
}

static enum signal_kind eval_stmt(struct lu_istate* state,
                                  struct ast_node* stmt) {
    switch (stmt->kind) {
        case AST_NODE_EXPR_STMT: {
            struct lu_value res = eval_expr(state, stmt->data.node);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return SIGNAL_ERROR;
            }
            break;
        }
        default: {
            break;
        }
    }
    return SIGNAL_NONE;
}

static enum signal_kind eval_stmts(struct lu_istate* state,
                                   struct ast_node** stmts) {
    const uint32_t nstmts = arrlen(stmts);
    for (uint32_t i = 0; i < nstmts; i++) {
        enum signal_kind sig = eval_stmt(state, stmts[i]);
        if (sig != SIGNAL_NONE) return sig;
    }

    return SIGNAL_NONE;
}

void lu_eval_program(struct lu_istate* state) {
    enum signal_kind signal =
        eval_stmts(state, state->context_stack->program.nodes);
    if (signal == SIGNAL_ERROR) {
        printf("%s\n", state->exception->message->data);
        // printf("Error: %s at line %ld\n", err->message, err->line);
    }
}
