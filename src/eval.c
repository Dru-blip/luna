#ifdef EVAL_IMPLEMENTATION
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
#include "tokenizer.h"
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
    state->running_module = nullptr;
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
    ctx->global_scope.variables = lu_object_new(state);
    return ctx;
}

static void delete_execution_context(struct lu_istate* state) {
    struct execution_context* ctx = state->context_stack;
    state->context_stack = ctx->prev;
    // arrfree(ctx->program.tokens);
    // free(ctx->program.source);
    // arena_destroy(&ctx->program.allocator);
    free(ctx);
}

struct lu_value lu_run_program(struct lu_istate* state, const char* filepath) {
    char* source = read_file(filepath);
    struct ast_program program = parse_program(filepath, source);
    state->context_stack =
        create_execution_context(state, state->context_stack);
    state->context_stack->filepath = filepath;
    state->context_stack->program = program;
    struct lu_module* module =
        lu_module_new(state, lu_string_new(state, filepath), &program);
    struct call_frame* frame = push_call_frame(state->context_stack);
    struct lu_module* prev_module = state->running_module;
    frame->scopes = nullptr;
    frame->module = module;
    state->running_module = module;
    lu_eval_program(state);
    frame = pop_call_frame(state->context_stack);
    struct lu_value result = frame->return_value;
    module->exported = result;
    lu_obj_set(state->module_cache, module->name, lu_value_object(module));
    delete_execution_context(state);
    free(frame);
    state->running_module = prev_module;
    return result;
}

struct call_frame* push_call_frame(struct execution_context* ctx) {
    struct call_frame* frame = calloc(1, sizeof(struct call_frame));
    frame->parent = ctx->call_stack;
    ctx->call_stack = frame;
    frame->self = nullptr;
    frame->scopes = nullptr;
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
    return scope;
}

struct scope* new_scope_with(struct heap* heap, struct scope* parent,
                             struct lu_object* variables) {
    struct scope* scope = calloc(1, sizeof(struct scope));
    scope->variables = variables;
    return scope;
}

static void begin_scope(struct lu_istate* state) {
    struct scope new_scope;
    new_scope.variables = lu_object_new(state);
    arrput(state->context_stack->call_stack->scopes, new_scope);
}

static void end_scope(struct lu_istate* state) {
    arrpop(state->context_stack->call_stack->scopes);
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

struct lu_value coerce_none_to_int(struct lu_value* v) {
    return (struct lu_value){.type = VALUE_INTEGER, .integer = 0};
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
            [VALUE_NONE] =
                {
                    .result_type = VALUE_INTEGER,
                    .coerce_lhs = coerce_bool_to_int,
                    .coerce_rhs = coerce_none_to_int,
                },
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
            [VALUE_NONE] =
                {
                    .result_type = VALUE_INTEGER,
                    .coerce_lhs = coerce_identity,
                    .coerce_rhs = coerce_none_to_int,
                },
            [VALUE_OBJECT] = INVALID_PROMOTION,
        },

    [VALUE_NONE] =
        {
            [VALUE_INTEGER] =
                {
                    .result_type = VALUE_INTEGER,
                    .coerce_lhs = coerce_none_to_int,
                    .coerce_rhs = coerce_identity,
                },
            [VALUE_BOOL] =
                {
                    .result_type = VALUE_INTEGER,
                    .coerce_lhs = coerce_none_to_int,
                    .coerce_rhs = coerce_bool_to_int,
                },
            [VALUE_NONE] =
                {
                    .result_type = VALUE_NONE,
                    .coerce_lhs = coerce_identity,
                    .coerce_rhs = coerce_identity,
                },
            [VALUE_OBJECT] = INVALID_PROMOTION,
        },
    [VALUE_OBJECT] =
        {
            [VALUE_INTEGER] = INVALID_PROMOTION,
            [VALUE_BOOL] = INVALID_PROMOTION,
            [VALUE_NONE] = INVALID_PROMOTION,
            [VALUE_OBJECT] =
                {
                    .result_type = VALUE_OBJECT,
                    .coerce_lhs = coerce_identity,
                    .coerce_rhs = coerce_identity,
                },
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
            [OP_LAND] = &lu_bool_and,
            [OP_LOR] = &lu_bool_or,
        },
};

static inline void declare_variable(struct lu_istate* state,
                                    struct lu_string* name,
                                    struct lu_value value) {
    size_t len = arrlen(state->context_stack->call_stack->scopes);
    if (len > 0) {
        lu_obj_set(state->context_stack->call_stack->scopes[len - 1].variables,
                   name, value);
    } else {
        lu_obj_set(state->context_stack->global_scope.variables, name, value);
    }
}

static inline int set_variable(struct lu_istate* state, struct lu_string* name,
                               struct lu_value value) {
    struct execution_context* ctx = state->context_stack;
    struct call_frame* frame = ctx->call_stack;

    size_t len = arrlen(frame->scopes);
    for (size_t i = len; i > 0; i--) {
        struct scope* scope = &frame->scopes[i - 1];
        struct lu_value existing = lu_obj_get(scope->variables, name);
        if (!lu_is_undefined(existing)) {
            lu_obj_set(scope->variables, name, value);
            return 0;
        }
    }

    struct lu_value global_val = lu_obj_get(ctx->global_scope.variables, name);
    if (!lu_is_undefined(global_val)) {
        lu_obj_set(ctx->global_scope.variables, name, value);
        return 0;
    }
    return 1;
}

static inline struct lu_value get_variable(struct lu_istate* state,
                                           struct lu_string* name) {
    struct execution_context* ctx = state->context_stack;
    struct call_frame* frame = ctx->call_stack;

    size_t len = arrlen(frame->scopes);
    for (size_t i = len; i > 0; i--) {
        struct scope* scope = &frame->scopes[i - 1];
        struct lu_value val = lu_obj_get(scope->variables, name);
        if (!lu_is_undefined(val)) return val;
    }

    struct lu_value val = lu_obj_get(ctx->global_scope.variables, name);
    if (!lu_is_undefined(val)) return val;

    return lu_obj_get(state->global_object, name);
}

struct lu_string* get_identifier(struct lu_istate* state, struct span* span) {
    const size_t len = span->end - span->start;
    char* buffer = malloc(len + 1);
    memcpy(
        buffer,
        state->context_stack->call_stack->module->program.source + span->start,
        len);
    buffer[len] = '\0';
    // printf("%s \n ", buffer);
    struct lu_string* str = lu_intern_string(state, buffer);
    free(buffer);
    return str;
}

static void eval_call(struct lu_istate* state, struct lu_object* self,
                      struct span* call_location, struct lu_function* func,
                      struct argument* args, struct lu_value* result) {
    // Implementation of eval_call function
    struct call_frame* frame = push_call_frame(state->context_stack);
    frame->function = func;
    frame->self = self;
    frame->call_location = *call_location;
    frame->module = func->module;
    struct ast_node body = *func->body;
    frame->scopes = nullptr;
    struct scope frame_scope;
    frame_scope.variables = lu_object_new(state);
    arrput(frame->scopes, frame_scope);
    // frame->scope = new_scope(state, nullptr);

    begin_scope(state);
    for (uint8_t i = 0; i < func->param_count; i++) {
        set_variable(state, args[i].name, args[i].value);
    }
    enum signal_kind sig = eval_stmt(state, &body);
    end_scope(state);
    frame = pop_call_frame(state->context_stack);
    *result = frame->return_value;
    free(frame);
}

// TODO: The current error handling repeatedly checks `state->op_result`
// after each eval. This slows down execution due to frequent branching.
// Should consider `setjmp`/`longjmp` to implement structured exception
// handling, allows immediate unwinding to a safe recovery point when  error
// is raised.

static struct lu_value eval_expr(struct lu_istate* state,
                                 struct ast_node* expr) {
    switch (expr->kind) {
        case AST_NODE_INT: {
            return lu_value_int(expr->data.int_val);
        }
        case AST_NODE_BOOL: {
            return lu_value_bool(expr->data.int_val);
        }
        case AST_NODE_NONE: {
            return lu_value_none();
        }
        case AST_NODE_STR: {
            const size_t len = expr->span.end - 1 - expr->span.start - 1;
            char* buffer = arena_alloc(&state->args_buffer, len);
            memcpy(buffer, expr->data.id, len);
            buffer[len] = '\0';
            struct lu_string* str = lu_string_new(state, buffer);
            arena_reset(&state->args_buffer);
            return lu_value_object(str);
        }
        case AST_NODE_IDENTIFIER: {
            struct lu_string* name = get_identifier(state, &expr->span);
            struct lu_value value = get_variable(state, name);
            if (lu_is_undefined(value)) {
                lu_raise_error(state,
                               lu_string_new(state, "undeclared indentifier"),
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
        case AST_NODE_OBJECT_EXPR: {
            struct lu_object* obj = lu_object_new(state);
            const size_t len = arrlen(expr->data.list);
            struct ast_node* prop;
            for (size_t i = 0; i < len; i++) {
                prop = expr->data.list[i];
                struct lu_string* key =
                    get_identifier(state, &prop->data.property.property_name);
                struct lu_value value;
                if (prop->data.property.shorthand) {
                    value = get_variable(state, key);
                    if (lu_is_undefined(value)) {
                        lu_raise_error(
                            state,
                            lu_string_new(state, "undeclared indentifier"),
                            &expr->span);
                        return value;
                    }
                } else {
                    value = eval_expr(state, prop->data.property.value);
                    if (state->op_result == OP_RESULT_RAISED_ERROR) {
                        return lu_value_none();
                    }
                }
                lu_obj_set(obj, key, value);
            }
            return lu_value_object(obj);
        }
        case AST_NODE_FN_EXPR: {
            const struct ast_fn_decl* fn_expr = &expr->data.fn_decl;
            struct lu_function* func_obj = lu_function_new(
                state, lu_intern_string(state, "anonymous"),
                state->running_module, fn_expr->params, fn_expr->body);
            return lu_value_object(func_obj);
        }
        case AST_NODE_SELF_EXPR: {
            struct lu_object* s = state->context_stack->call_stack->self;
            return s ? lu_value_object(s) : lu_value_none();
        }
        case AST_NODE_UNOP: {
            struct lu_value argument =
                eval_expr(state, expr->data.unop.argument);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return argument;
            }
            if (expr->data.unop.op == OP_NEGATE) {
                if (lu_is_int(argument)) {
                    return lu_value_int(-argument.integer);
                }
                lu_raise_error(
                    state,
                    lu_string_new(state, "cannot negate non-integer value"),
                    &expr->span);
                return argument;
            } else if (expr->data.unop.op == OP_UPLUS) {
                if (!lu_is_int(argument)) {
                    lu_raise_error(
                        state,
                        lu_string_new(
                            state, "unsupported operand type for unary plus"),
                        &expr->span);
                }
                return argument;
            } else {
                return lu_value_bool(!lu_is_truthy(argument));
            }
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

            // TODO: find a better way to handle operations.
            //
            //  if operator is equality(== ,!=)
            if (expr->data.binop.op == OP_EQ) {
                return lu_value_bool(lu_value_strict_equals(lhs, rhs));
            }

            if (expr->data.binop.op == OP_NEQ) {
                return lu_value_bool(!lu_value_strict_equals(lhs, rhs));
            }

            if (lu_is_string(lhs) || lu_is_string(rhs)) {
                return lu_value_object(lu_string_concat(state, lhs, rhs));
            }

            if (lhs.type == rhs.type) {
                binaryfunc func =
                    binop_dispatch_table[lhs.type][expr->data.binop.op];

                if (!func) {
                    goto raise_unsupported;
                }
                return func(state, &lhs, &rhs);
            }

            struct type_promotion promotion =
                type_promotion_table[lhs.type][rhs.type];

            if (promotion.result_type == -1) {
                goto raise_unsupported;
            }

            lhs = promotion.coerce_lhs(&lhs);
            rhs = promotion.coerce_rhs(&rhs);

            binaryfunc func =
                binop_dispatch_table[lhs.type][expr->data.binop.op];

            if (!func) {
                goto raise_unsupported;
            }

            return func(state, &lhs, &rhs);
        raise_unsupported:
            const char* lhs_type_name = lu_value_get_type_name(lhs);
            const char* rhs_type_name = lu_value_get_type_name(rhs);
            char buffer[256];
            snprintf(buffer, sizeof(buffer),
                     "invalid operand types for operation (%s) : '%s' and '%s'",
                     binary_op_symbols[expr->data.binop.op], lhs_type_name,
                     rhs_type_name);
            lu_raise_error(state, lu_string_new(state, buffer), &expr->span);
            return lu_value_none();
        }
        case AST_NODE_ASSIGN: {
            struct lu_value value = eval_expr(state, expr->data.binop.rhs);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return value;
            }
            switch (expr->data.binop.lhs->kind) {
                case AST_NODE_IDENTIFIER: {
                    struct lu_string* name =
                        get_identifier(state, &expr->data.binop.lhs->span);
                    if (set_variable(state, name, value) == 1) {
                        lu_raise_error(
                            state, lu_string_new(state, "undeclared variable"),
                            &expr->span);
                    }
                    break;
                }
                case AST_NODE_MEMBER_EXPR: {
                    struct lu_value obj = eval_expr(
                        state, expr->data.binop.lhs->data.member_expr.object);
                    if (state->op_result == OP_RESULT_RAISED_ERROR) {
                        return lu_value_none();
                    }
                    if (!lu_is_object(obj)) {
                        goto invalid_assignment;
                    }
                    struct lu_string* name = get_identifier(
                        state,
                        &expr->data.binop.lhs->data.member_expr.property_name);
                    lu_obj_set(lu_as_object(obj), name, value);
                    break;
                }
                case AST_NODE_COMPUTED_MEMBER_EXPR: {
                    /*
                     * Duplicate block of code starts
                     */
                    struct lu_value obj =
                        eval_expr(state, expr->data.binop.lhs->data.pair.fst);
                    if (state->op_result == OP_RESULT_RAISED_ERROR) {
                        return lu_value_none();
                    }
                    struct lu_value prop =
                        eval_expr(state, expr->data.binop.lhs->data.pair.snd);
                    if (state->op_result == OP_RESULT_RAISED_ERROR) {
                        return lu_value_none();
                    }
                    if (lu_is_array(obj)) {
                        if (lu_is_int(prop)) {
                            int64_t index = lu_as_int(prop);
                            if (index < 0) {
                                lu_raise_error(
                                    state,
                                    lu_string_new(state, "invalid index"),
                                    &expr->span);
                                return lu_value_none();
                            }
                            if (lu_array_set(lu_as_array(obj), index, value) !=
                                0) {
                                char buffer[256];
                                snprintf(buffer, sizeof(buffer),
                                         "index %ld out of bounds (array "
                                         "length %ld)",
                                         index,
                                         lu_array_length(lu_as_array(obj)));
                                lu_raise_error(state,
                                               lu_string_new(state, buffer),
                                               &expr->span);
                            }
                            break;
                        }
                        char buffer[256];
                        snprintf(buffer, sizeof(buffer),
                                 "array index must be an integer ,got %s",
                                 lu_value_get_type_name(prop));
                        lu_raise_error(state, lu_string_new(state, buffer),
                                       &expr->span);
                        return lu_value_none();
                    }
                    if (!lu_is_string(prop)) {
                        char buffer[256];
                        snprintf(buffer, sizeof(buffer),
                                 "object accessor must be a string , got %s",
                                 lu_value_get_type_name(prop));
                        lu_raise_error(state, lu_string_new(state, buffer),
                                       &expr->span);
                        return lu_value_none();
                    }
                    lu_obj_set(lu_as_object(obj), lu_as_string(prop), value);
                    break;
                    /*
                     * Duplicate block of code Ends
                     */
                }
                default: {
                    goto invalid_assignment;
                }
            }

            return value;
        invalid_assignment:
            lu_raise_error(state,
                           lu_string_new(state, "invalid assignment target"),
                           &expr->span);
            return lu_value_none();
        }
        case AST_NODE_CALL: {
            struct lu_value callee;
            struct lu_value self = lu_value_undefined();
            struct ast_call* call = &expr->data.call;

            // populate self object
            switch (call->callee->kind) {
                case AST_NODE_MEMBER_EXPR: {
                    // Duplicate code inside member expression evaluation.
                    // TODO: Refactor this code to avoid duplication.
                    self =
                        eval_expr(state, call->callee->data.member_expr.object);
                    if (!lu_is_object(self)) {
                        lu_raise_error(
                            state,
                            lu_string_new(state,
                                          "invalid member access on non "
                                          "object value"),
                            &expr->span);
                        return lu_value_none();
                    }
                    break;
                }
                case AST_NODE_COMPUTED_MEMBER_EXPR: {
                    // Duplicate code inside AST_NODE_COMPUTED_MEMBER_EXPR
                    // evaluation
                    // TODO: Refactor this code to avoid duplication.
                    self = eval_expr(state, expr->data.pair.fst);
                    if (state->op_result == OP_RESULT_RAISED_ERROR) {
                        return lu_value_none();
                    }
                    if (!lu_is_object(self)) {
                        lu_raise_error(
                            state,
                            lu_string_new(state,
                                          "invalid member access on non "
                                          "object value"),
                            &expr->span);
                        return lu_value_none();
                    }
                    break;
                }
                default: {
                    break;
                }
            }

            callee = eval_expr(state, call->callee);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return lu_value_none();
            }

            if (!lu_is_function(callee)) {
                // TODO: Raise errors for uncallable objects
                const char* calle_type_name = lu_value_get_type_name(callee);
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                         "attempt to call a non function value"
                         " (%s)",
                         calle_type_name);
                lu_raise_error(state, lu_string_new(state, buffer),
                               &expr->span);
                return lu_value_none();
            }

            struct lu_function* func = lu_as_function(callee);
            struct argument* args = arena_alloc(
                &state->args_buffer, sizeof(struct argument) * (call->argc));

            for (uint32_t i = 0; i < call->argc; ++i) {
                if (func->type == FUNCTION_USER) {
                    struct lu_string* arg_name =
                        get_identifier(state, &func->params[i]->span);
                    args[i].name = arg_name;
                }
                args[i].value = eval_expr(state, call->args[i]);
                if (state->op_result == OP_RESULT_RAISED_ERROR) {
                    arena_reset(&state->args_buffer);
                    return lu_value_none();
                }
            }

            struct lu_object* self_obj = lu_is_undefined(self)
                                             ? lu_cast(struct lu_object, func)
                                             : lu_as_object(self);
            struct lu_value res = lu_value_none();
            if (func->type == FUNCTION_NATIVE) {
                // move this eval call itself , it will decide whether to
                // dispatch (or) execute ast of function.
                struct call_frame* frame =
                    push_call_frame(state->context_stack);
                frame->function = func;
                frame->arg_count = call->argc;
                frame->call_location = expr->span;
                frame->self = self_obj;
                frame->module = state->running_module;
                res = func->func(state, args);
                pop_call_frame(state->context_stack);
            } else {
                struct argument arg = args[0];
                eval_call(state, self_obj, &expr->span, func, args, &res);
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
                                  "invalid member access on non object value"),
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
                strbuf_appendf(&sb, "object has no property '%s'",
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
                                  "invalid member access on non object value"),
                    &expr->span);
                return lu_value_none();
            }
            struct lu_value prop = eval_expr(state, expr->data.pair.snd);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return lu_value_none();
            }
            // implement subscript access for strings.
            // maybe use vtables for subscript implementation.
            if (lu_is_array(val)) {
                if (lu_is_int(prop)) {
                    int64_t index = lu_as_int(prop);
                    if (index < 0) {
                        // TODO: should be more descriptive message
                        lu_raise_error(state,
                                       lu_string_new(state, "invalid index"),
                                       &expr->span);
                        return lu_value_none();
                    }

                    struct lu_value result =
                        lu_array_get(lu_as_array(val), index);
                    if (lu_is_undefined(result)) {
                        char buffer[256];
                        snprintf(buffer, sizeof(buffer),
                                 "index %ld out of bounds (array length %ld)",
                                 index, lu_array_length(lu_as_array(val)));
                        lu_raise_error(state, lu_string_new(state, buffer),
                                       &expr->span);
                    }
                    return result;
                }
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                         "array index must be an integer ,got %s",
                         lu_value_get_type_name(prop));
                lu_raise_error(state, lu_string_new(state, buffer),
                               &expr->span);
                return lu_value_none();
            }
            if (!lu_is_string(prop)) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                         "object accessor must be a string , got %s",
                         lu_value_get_type_name(prop));
                lu_raise_error(state, lu_string_new(state, buffer),
                               &expr->span);
                return lu_value_none();
            }
            struct lu_value result =
                lu_obj_get(lu_as_object(val), lu_as_string(prop));
            if (lu_is_undefined(result)) {
                char buffer[256];
                struct strbuf sb;
                strbuf_init_static(&sb, buffer, sizeof(buffer));
                strbuf_appendf(&sb, "object has no property '%s'",
                               lu_string_get_cstring(lu_as_string(prop)));
                lu_raise_error(state, lu_string_new(state, buffer),
                               &expr->span);
                return lu_value_none();
            }
            return result;
        }
        default: {
            return lu_value_none();
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
                lu_function_new(state, name, state->running_module,
                                fndecl->params, fndecl->body);
            declare_variable(state, name, lu_value_object(func_obj));
            break;
        }
        case AST_NODE_LET_DECL: {
            struct ast_let_decl* letdecl = &stmt->data.let_decl;

            struct lu_string* name = get_identifier(state, &letdecl->name_span);
            struct lu_value value = eval_expr(state, letdecl->value);
            if (state->op_result == OP_RESULT_RAISED_ERROR) {
                return SIGNAL_ERROR;
            }
            declare_variable(state, name, value);
            break;
        }
        case AST_NODE_BLOCK: {
            begin_scope(state);
            enum signal_kind sig = eval_stmts(state, stmt->data.list);
            end_scope(state);
            return sig;
        }
        case AST_NODE_IF_STMT: {
            struct lu_value cond = eval_expr(state, stmt->data.if_stmt.test);
            if (lu_is_truthy(cond)) {
                return eval_stmt(state, stmt->data.if_stmt.consequent);
            }
            if (stmt->data.if_stmt.alternate) {
                return eval_stmt(state, stmt->data.if_stmt.alternate);
            }
            break;
        }
        case AST_NODE_BREAK_STMT: {
            return SIGNAL_BREAK;
        }
        case AST_NODE_CONTINUE_STMT: {
            return SIGNAL_CONTINUE;
        }
        case AST_NODE_LOOP_STMT: {
            begin_scope(state);
        loop_start:
            enum signal_kind sig = eval_stmt(state, stmt->data.node);
            if (sig == SIGNAL_BREAK) goto loop_end;
            if (sig == SIGNAL_CONTINUE) goto loop_start;
            if (sig == SIGNAL_RETURN || sig == SIGNAL_ERROR) return sig;
            goto loop_start;
        loop_end:
            end_scope(state);
        }
        case AST_NODE_WHILE_STMT: {
            const struct ast_pair* pair = &stmt->data.pair;
        w_loop_start:
            struct lu_value cond = eval_expr(state, pair->fst);
            if (!(lu_is_truthy(cond))) {
                goto w_loop_end;
            }
            enum signal_kind sig = eval_stmt(state, pair->snd);
            if (sig == SIGNAL_BREAK) goto w_loop_end;
            if (sig == SIGNAL_CONTINUE) goto w_loop_start;
            if (sig == SIGNAL_RETURN || sig == SIGNAL_ERROR) return sig;
            goto w_loop_start;
        w_loop_end:
            break;
        }
        case AST_NODE_FOR_STMT: {
            const struct ast_for_stmt* for_stmt = &stmt->data.for_stmt;
            begin_scope(state);
            eval_stmt(state, for_stmt->init);
        f_loop_start:
            struct lu_value cond = eval_expr(state, for_stmt->test);
            if (!(lu_is_truthy(cond))) {
                goto f_loop_end;
            }
            enum signal_kind sig = eval_stmt(state, for_stmt->body);
            if (sig == SIGNAL_BREAK) goto f_loop_end;
            if (sig == SIGNAL_CONTINUE) goto f_loop_update;
            if (sig == SIGNAL_RETURN || sig == SIGNAL_ERROR) return sig;
        f_loop_update:
            eval_expr(state, for_stmt->update);
            goto f_loop_start;
        f_loop_end:
            end_scope(state);
            break;
        }
        case AST_NODE_FOR_IN_STMT: {
            const struct ast_for_in_stmt* for_in_stmt = &stmt->data.for_in_stmt;
            if (for_in_stmt->left->kind != AST_NODE_IDENTIFIER) {
                lu_raise_error(
                    state,
                    lu_string_new(state, "loop variable must be an identifier"),
                    &for_in_stmt->left->span);
                return SIGNAL_ERROR;
            }
            struct lu_value iterable = eval_expr(state, for_in_stmt->right);
            if (!lu_is_array(iterable)) {
                lu_raise_error(
                    state,
                    lu_string_new(
                        state, "only array iteration is supported currently"),
                    &SPAN_MERGE(stmt->span, for_in_stmt->right->span));
                return SIGNAL_ERROR;
            }
            begin_scope(state);
            struct lu_string* loop_var_name =
                get_identifier(state, &for_in_stmt->left->span);
            struct lu_array_iter iter =
                lu_array_iter_new(lu_as_array(iterable));
            declare_variable(state, loop_var_name, lu_value_none());
        in_loop_start:
            struct lu_value value = lu_array_iter_next(&iter);
            if (lu_is_undefined(value)) {
                goto in_loop_end;
            }
            set_variable(state, loop_var_name, value);
            enum signal_kind sig = eval_stmt(state, for_in_stmt->body);
            if (sig == SIGNAL_BREAK) goto in_loop_end;
            if (sig == SIGNAL_CONTINUE) goto in_loop_start;
            if (sig == SIGNAL_RETURN || sig == SIGNAL_ERROR) return sig;
            goto in_loop_start;
        in_loop_end:
            end_scope(state);
            break;
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
#endif
