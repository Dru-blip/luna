#include "eval.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "ast.h"
#include "eval.h"
#include "heap.h"
#include "operator.h"
#include "parser.h"
#include "stb_ds.h"
#include "strbuf.h"
#include "string_interner.h"
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
    string_interner_init(state);
    state->global_object = lu_object_new(state);
    state->module_cache = lu_object_new(state);
    lu_init_global_object(state);
    arena_init(&state->args_buffer);
    return state;
}

void lu_istate_destroy(struct lu_istate* state) {
    string_interner_destroy(&state->string_pool);
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

struct lu_value lu_run_program(struct lu_istate* state, const char* filepath) {
    char* source = read_file(filepath);
    struct ast_program program = parse_program(filepath, source);
    state->context_stack =
        create_execution_context(state, state->context_stack);
    state->context_stack->filepath = filepath;
    state->context_stack->program = program;
    struct call_frame* frame = push_call_frame(state->context_stack);
    lu_eval_program(state);
    frame = pop_call_frame(state->context_stack);
    struct lu_value result = frame->return_value;
    delete_execution_context(state);
    free(frame);
    return result;
}

struct call_frame* push_call_frame(struct execution_context* ctx) {
    struct call_frame* frame = calloc(1, sizeof(struct call_frame));
    frame->parent = ctx->call_stack;
    ctx->call_stack = frame;
    frame->return_value = lu_value_none();
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
    scope->variables = lu_object_new(state);
    scope->depth = parent ? parent->depth + 1 : 0;
    scope->parent = parent;
    return scope;
}

struct scope* new_scope_with(struct heap* heap, struct scope* parent,
                             struct lu_object* variables) {
    struct scope* scope = calloc(1, sizeof(struct scope));
    scope->variables = variables;
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

static enum signal_kind eval_stmts(struct lu_istate* state,
                                   struct ast_node** stmts);
static enum signal_kind eval_stmt(struct lu_istate* state,
                                  struct ast_node* stmt);

typedef struct lu_value (*binaryfunc)(struct lu_istate* state, struct lu_value*,
                                      struct lu_value*);
typedef struct lu_value (*coerce_fn)(struct lu_value*);
typedef struct lu_value (*unaryfunc)(struct lu_value*);

struct type_promotion {
    enum lu_value_type result_type;
    coerce_fn coerce_lhs;
    coerce_fn coerce_rhs;
};

struct lu_value coerce_identity(struct lu_value* v) { return *v; }
struct lu_value coerce_bool_to_int(struct lu_value* v) {
    return (struct lu_value){.type = VALUE_INTEGER, .integer = v->integer};
}

#define DEFINE_BINOP(TYPE, NAME, RESULT_TYPE, OP)                          \
    struct lu_value lu_##TYPE##_##NAME(                                    \
        struct lu_istate* state, struct lu_value* a, struct lu_value* b) { \
        return (struct lu_value){.type = RESULT_TYPE,                      \
                                 .integer = a->integer OP b->integer};     \
    }

DEFINE_BINOP(int, add, VALUE_INTEGER, +)
DEFINE_BINOP(int, sub, VALUE_INTEGER, -)
DEFINE_BINOP(int, mul, VALUE_INTEGER, *)

DEFINE_BINOP(int, lt, VALUE_BOOL, <)
DEFINE_BINOP(int, lte, VALUE_BOOL, <=)
DEFINE_BINOP(int, gt, VALUE_BOOL, >)
DEFINE_BINOP(int, gte, VALUE_BOOL, >=)
DEFINE_BINOP(int, eq, VALUE_BOOL, ==);
DEFINE_BINOP(int, neq, VALUE_BOOL, !=)
DEFINE_BINOP(int, and, VALUE_BOOL, &&);
DEFINE_BINOP(int, or, VALUE_BOOL, ||);

DEFINE_BINOP(bool, add, VALUE_INTEGER, +)
DEFINE_BINOP(bool, sub, VALUE_INTEGER, -)
DEFINE_BINOP(bool, mul, VALUE_INTEGER, *)

DEFINE_BINOP(bool, lt, VALUE_BOOL, <)
DEFINE_BINOP(bool, lte, VALUE_BOOL, <=)
DEFINE_BINOP(bool, gt, VALUE_BOOL, >)
DEFINE_BINOP(bool, gte, VALUE_BOOL, >=)
DEFINE_BINOP(bool, eq, VALUE_BOOL, ==)
DEFINE_BINOP(bool, neq, VALUE_BOOL, !=)
DEFINE_BINOP(bool, and, VALUE_BOOL, &&);
DEFINE_BINOP(bool, or, VALUE_BOOL, ||);

#define INVALID_PROMOTION      \
    {                          \
        .result_type = -1,     \
        .coerce_lhs = nullptr, \
        .coerce_rhs = nullptr, \
    }

struct type_promotion type_promotion_table[5][5] = {
    [VALUE_BOOL] =
        {
            [VALUE_BOOL] =
                {
                    .result_type = VALUE_BOOL,
                    .coerce_lhs = coerce_bool_to_int,
                    .coerce_rhs = coerce_bool_to_int,
                },
            [VALUE_INTEGER] =
                {
                    .result_type = VALUE_INTEGER,
                    .coerce_lhs = coerce_bool_to_int,
                    .coerce_rhs = coerce_identity,
                },
            [VALUE_NONE] = INVALID_PROMOTION,
            [VALUE_OBJECT] = INVALID_PROMOTION,
        },

    [VALUE_INTEGER] =
        {
            [VALUE_BOOL] =
                {
                    .result_type = VALUE_INTEGER,
                    .coerce_lhs = coerce_identity,
                    .coerce_rhs = coerce_bool_to_int,
                },
            [VALUE_INTEGER] =
                {
                    .result_type = VALUE_INTEGER,
                    .coerce_lhs = coerce_identity,
                    .coerce_rhs = coerce_identity,
                },
            [VALUE_NONE] = INVALID_PROMOTION,
            [VALUE_OBJECT] = INVALID_PROMOTION,
        },

    [VALUE_NONE] =
        {
            [VALUE_INTEGER] = INVALID_PROMOTION,
            [VALUE_BOOL] = INVALID_PROMOTION,
            [VALUE_NONE] = {.result_type = VALUE_NONE,
                            .coerce_lhs = coerce_identity,
                            .coerce_rhs = coerce_identity},
            [VALUE_OBJECT] = INVALID_PROMOTION,
        },
};

static binaryfunc binop_dispatch_table[5][15] = {
    [VALUE_INTEGER] =
        {
            [OP_ADD] = &lu_int_add,
            [OP_SUB] = &lu_int_sub,
            [OP_MUL] = &lu_int_mul,

            [OP_LT] = &lu_int_lt,
            [OP_GT] = &lu_int_gt,
            [OP_LTE] = &lu_int_lte,
            [OP_GTE] = &lu_int_gte,
            [OP_EQ] = &lu_int_eq,
            [OP_NEQ] = &lu_int_neq,
            [OP_LAND] = &lu_int_and,
            [OP_LOR] = &lu_int_or,

        },
    [VALUE_BOOL] =
        {
            [OP_ADD] = &lu_bool_add,
            [OP_SUB] = &lu_bool_sub,
            [OP_MUL] = &lu_bool_mul,

            [OP_LT] = &lu_bool_lt,
            [OP_GT] = &lu_bool_gt,
            [OP_LTE] = &lu_bool_lte,
            [OP_GTE] = &lu_bool_gte,
            [OP_EQ] = &lu_bool_eq,
            [OP_NEQ] = &lu_bool_neq,
            [OP_LAND] = &lu_bool_and,
            [OP_LOR] = &lu_bool_or,
        },
};

static inline void set_variable(struct lu_istate* state, struct lu_string* name,
                                struct lu_value value) {
    struct scope* scope = state->context_stack->scope;
    while (scope) {
        if (!lu_is_undefined(lu_obj_get(scope->variables, name))) {
            break;
        }
        scope = scope->parent;
    }
    if (scope) {
        lu_obj_set(scope->variables, name, value);
    } else {
        lu_obj_set(state->context_stack->scope->variables, name, value);
    }
}

static inline struct lu_value get_variable(struct lu_istate* state,
                                           struct lu_string* name) {
    struct scope* scope = state->context_stack->scope;
    struct lu_value val = lu_value_undefined();
    while (scope) {
        val = lu_obj_get(scope->variables, name);
        if (!lu_is_undefined(val)) {
            return val;
        }
        scope = scope->parent;
    }
    val = lu_obj_get(state->global_object, name);
    if (lu_is_undefined(val)) {
        return val;
    }
    return val;
}

struct lu_string* get_identifier(struct lu_istate* state, struct span* span) {
    const size_t len = span->end - span->start;
    char* buffer = malloc(len + 1);
    memcpy(buffer, state->context_stack->program.source + span->start, len);
    buffer[len] = '\0';
    struct lu_string* str = lu_intern_string(state, buffer);
    free(buffer);
    return str;
}

static void eval_call(struct lu_istate* state, struct span* call_location,
                      struct lu_function* func, struct argument* args,
                      struct lu_value* result) {
    // Implementation of eval_call function
    struct call_frame* frame = push_call_frame(state->context_stack);
    frame->function = func;
    frame->self = func;
    frame->call_location = *call_location;

    begin_scope(state);
    for (uint8_t i = 0; i < func->param_count; i++) {
        set_variable(state, args[i].name, args[i].value);
    }
    enum signal_kind sig = eval_stmt(state, func->body);
    end_scope(state);
    frame = pop_call_frame(state->context_stack);
    *result = frame->return_value;
    free(frame);
}

static struct lu_value eval_expr(struct lu_istate* state,
                                 struct ast_node* expr) {
    switch (expr->kind) {
        case AST_NODE_INT: {
            return lu_value_int(expr->data.int_val);
        }
        case AST_NODE_BOOL: {
            return lu_value_bool(expr->data.int_val);
        }
        case AST_NODE_STR: {
            const size_t len = expr->span.end - 1 - expr->span.start - 1;
            char* buffer = arena_alloc(&state->args_buffer, len);
            memcpy(buffer, expr->data.id, len);
            struct lu_string* str = lu_string_new(state, buffer);
            arena_reset(&state->args_buffer);
            return lu_value_object(str);
        }
        case AST_NODE_IDENTIFIER: {
            struct lu_string* name = get_identifier(state, &expr->span);
            struct lu_value value = get_variable(state, name);
            if (lu_is_undefined(value)) {
                lu_raise_error(state,
                               lu_string_new(state, "Undeclared indentifier"),
                               &expr->span);
            }
            return value;
        }
        case AST_NODE_ARRAY_EXPR: {
            struct lu_array* array = lu_array_new(state);
            const size_t len = arrlen(expr->data.list);
            for (size_t i = 0; i < len; ++i) {
                struct lu_value elem = eval_expr(state, expr->data.list[i]);
                if (state->op_result == OP_RESULT_RAISED_ERROR) {
                    goto ret;
                }
                lu_array_push(array, elem);
            }

        ret:
            return lu_value_object(array);
        }
        case AST_NODE_BINOP: {
            struct lu_value lhs = eval_expr(state, expr->data.binop.lhs);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return lhs;
            }
            struct lu_value rhs = eval_expr(state, expr->data.binop.rhs);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return rhs;
            }

            if (lhs.type == rhs.type) {
                binaryfunc func =
                    binop_dispatch_table[lhs.type][expr->data.binop.op];

                if (!func) {
                    // TODO:raise error.
                    state->op_result = OP_RESULT_RAISED_ERROR;
                    return lu_value_none();
                }
                return func(state, &lhs, &rhs);
            }
        }
        case AST_NODE_ASSIGN: {
            struct lu_value value = eval_expr(state, expr->data.binop.rhs);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return value;
            }
            struct lu_string* name =
                get_identifier(state, &expr->data.binop.lhs->span);
            set_variable(state, name, value);
            return value;
        }
        case AST_NODE_CALL: {
            struct lu_value callee = eval_expr(state, expr->data.call.callee);

            if (!lu_is_function(callee)) {
                state->op_result = OP_RESULT_RAISED_ERROR;
                return lu_value_none();
            }

            struct lu_function* func = lu_as_function(callee);
            struct argument* args =
                arena_alloc(&state->args_buffer,
                            sizeof(struct argument) * func->param_count);

            for (uint32_t i = 0; i < expr->data.call.argc; ++i) {
                if (func->type == FUNCTION_USER) {
                    args[i].name =
                        get_identifier(state, &func->params[i]->span);
                }
                args[i].value = eval_expr(state, expr->data.call.args[i]);
                if (state->op_result == OP_RESULT_RAISED_ERROR) {
                    arena_reset(&state->args_buffer);
                    return lu_value_none();
                }
            }

            struct lu_value res = lu_value_none();
            if (func->type == FUNCTION_NATIVE) {
                struct call_frame* frame =
                    push_call_frame(state->context_stack);
                frame->function = func;
                frame->self = func;
                frame->call_location = expr->span;
                res = func->func(state, args);
                pop_call_frame(state->context_stack);
            } else {
                eval_call(state, &expr->span, func, args, &res);
            }
            arena_reset(&state->args_buffer);
            return res;
        }
        case AST_NODE_MEMBER_EXPR: {
            struct lu_value val =
                eval_expr(state, expr->data.member_expr.object);
            if (!lu_is_object(val)) {
                lu_raise_error(
                    state,
                    lu_string_new(state,
                                  "Invalid member access on non object value"),
                    &expr->span);
                return lu_value_none();
            }
            struct lu_string* prop_name =
                get_identifier(state, &expr->data.member_expr.property_name);

            struct lu_value prop = lu_obj_get(lu_as_object(val), prop_name);
            if (lu_is_undefined(prop)) {
                char buffer[256];
                struct strbuf sb;
                strbuf_init_static(&sb, buffer, sizeof(buffer));
                strbuf_appendf(&sb, "Object has no property '%s'",
                               lu_string_get_cstring(prop_name));
                lu_raise_error(state, lu_string_new(state, buffer),
                               &expr->span);
                return lu_value_none();
            }

            return prop;
        }
        case AST_NODE_COMPUTED_MEMBER_EXPR: {
            struct lu_value val = eval_expr(state, expr->data.pair.fst);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return lu_value_none();
            }
            if (!lu_is_object(val)) {
                lu_raise_error(
                    state,
                    lu_string_new(state,
                                  "Invalid member access on non object value"),
                    &expr->span);
                return lu_value_none();
            }
            struct lu_value prop = eval_expr(state, expr->data.pair.snd);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return lu_value_none();
            }
            if (lu_is_array(val)) {
                if (lu_is_int(prop)) {
                    int64_t index = lu_as_int(prop);
                    if (index < 0) {
                        lu_raise_error(state,
                                       lu_string_new(state, "Invalid index"),
                                       &expr->span);
                        return lu_value_none();
                    }

                    struct lu_value result =
                        lu_array_get(lu_as_array(val), index);
                    if (lu_is_undefined(result)) {
                        lu_raise_error(
                            state, lu_string_new(state, "Index out of bounds"),
                            &expr->span);
                    }
                    return result;
                }
                lu_raise_error(state,
                               lu_string_new(state, "Invalid index type"),
                               &expr->span);
                return lu_value_none();
            }
            if (!lu_is_string(prop)) {
                lu_raise_error(state,
                               lu_string_new(state, "Invalid property type"),
                               &expr->span);
                return lu_value_none();
            }
            struct lu_value result =
                lu_obj_get(lu_as_object(val), lu_as_string(prop));
            if (lu_is_undefined(result)) {
                char buffer[256];
                struct strbuf sb;
                strbuf_init_static(&sb, buffer, sizeof(buffer));
                strbuf_appendf(&sb, "Object has no property '%s'",
                               lu_string_get_cstring(lu_as_string(prop)));
                lu_raise_error(state, lu_string_new(state, buffer),
                               &expr->span);
                return lu_value_none();
            }
            return result;
        }
        default: {
            break;
        }
    }
}

static enum signal_kind eval_stmt(struct lu_istate* state,
                                  struct ast_node* stmt) {
    switch (stmt->kind) {
        case AST_NODE_FN_DECL: {
            struct ast_fn_decl* fndecl = &stmt->data.fn_decl;

            struct lu_string* name = get_identifier(state, &fndecl->name_span);
            struct lu_function* func_obj =
                lu_function_new(state, name, fndecl->params, fndecl->body);
            set_variable(state, name, lu_value_object(func_obj));
            break;
        }
        case AST_NODE_BLOCK: {
            begin_scope(state);
            enum signal_kind sig = eval_stmts(state, stmt->data.list);
            end_scope(state);
            return sig;
        }
        case AST_NODE_EXPR_STMT: {
            struct lu_value res = eval_expr(state, stmt->data.node);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return SIGNAL_ERROR;
            }
            break;
        }
        case AST_NODE_RETURN: {
            struct lu_value res = eval_expr(state, stmt->data.node);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return SIGNAL_ERROR;
            }
            state->context_stack->call_stack->return_value = res;
            return SIGNAL_RETURN;
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
        if (state->context_stack->prev) return;
        if (state->error) {
            struct lu_string* str = lu_as_string(
                lu_obj_get(state->error, lu_intern_string(state, "message")));
            struct lu_string* traceback = lu_as_string(
                lu_obj_get(state->error, lu_intern_string(state, "traceback")));
            printf("Error: %s\n", lu_string_get_cstring(str));
            if (traceback) {
                printf("%s\n", traceback->block->data);
            }
        }
    }
}
