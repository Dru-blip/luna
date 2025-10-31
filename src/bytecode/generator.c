#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "bytecode/interpreter.h"
#include "bytecode/ir.h"
#include "operator.h"
#include "stb_ds.h"
#include "string_interner.h"
#include "tokenizer.h"
#include "value.h"

#define GET_CURRENT_BLOCK generator->blocks[generator->current_block_id]

static void executable_finalize(struct lu_object* obj) {
    lu_object_get_default_vtable()->finalize(obj);
}

static void executable_visit(struct lu_object* obj, struct lu_objectset* set) {
    struct executable* exe = lu_cast(struct executable, obj);
    for (size_t i = 0; i < exe->constants_size; i++) {
        if (lu_is_object(exe->constants[i])) {
            lu_objectset_add(set, exe->constants[i].object);
        }
    }

    lu_object_get_default_vtable()->visit(obj, set);
}

static struct lu_object_vtable lu_executable_vtable = {
    .is_array = false,
    .is_function = false,
    .is_string = false,
    .tag = OBJECT_TAG_EXECUTABLE,
    .finalize = executable_finalize,
    .visit = executable_visit,
};

void generator_init(struct generator* generator, struct ast_program program) {
    generator->program = program;
    generator->current_block_id = 0;
    generator->block_counter = 0;
    generator->constant_counter = 0;
    generator->register_counter = 1;
    generator->blocks = nullptr;
    generator->node = nullptr;
    generator->prev = nullptr;
    generator->constants = nullptr;
    generator->local_variables = nullptr;
    generator->global_variables = nullptr;
    generator->loop_stack = nullptr;
    generator->identifier_table = nullptr;

    generator->identifier_table_size = 0;
    generator->local_variable_count = 0;
    generator->global_variable_count = 0;
    generator->scope_depth = 0;
}

size_t generator_basic_block_new(struct generator* generator) {
    struct basic_block block;
    block.instructions = nullptr;
    block.instructions_spans = nullptr;
    block.id = generator->block_counter++;
    arrput(generator->blocks, block);
    return block.id;
}

static inline uint32_t generator_allocate_register(
    struct generator* generator) {
    return generator->register_counter++;
}

static void declare_variable(struct generator* generator, char* name,
                             uint32_t name_length, struct variable** result) {
    // check for local variable
    struct variable* var;
    for (uint32_t i = generator->local_variable_count; i > 0; i--) {
        var = &generator->local_variables[i - 1];
        if (var->name_length == name_length &&
            strncmp(var->name, name, name_length) == 0) {
            *result = var;
            return;
        }
    }

    // check for global variable , if it is not a local variable
    for (uint32_t i = generator->global_variable_count; i > 0; i--) {
        var = &generator->global_variables[i - 1];
        if (var->name_length == name_length &&
            strncmp(var->name, name, name_length) == 0) {
            *result = var;
            return;
        }
    }

    // if variable was not declared anywhere,create a new variable
    uint32_t scope_depth = generator->scope_depth;
    struct variable new_variable = {
        .name = name,
        .name_length = name_length,
        .scope = scope_depth == 0 ? SCOPE_GLOBAL : SCOPE_LOCAL,
        .scope_depth = scope_depth,
    };
    uint32_t slot = new_variable.scope == SCOPE_GLOBAL
                        ? generator->global_variable_count++
                        : generator_allocate_register(generator);
    new_variable.allocated_reg = slot;
    if (new_variable.scope == SCOPE_GLOBAL) {
        arrput(generator->global_variables, new_variable);
        *result =
            &generator->global_variables[generator->global_variable_count - 1];
    } else {
        arrput(generator->local_variables, new_variable);
        generator->local_variable_count++;
        *result =
            &generator->local_variables[arrlen(generator->local_variables) - 1];
    }
}

static bool find_variable(struct generator* generator, char* name,
                          uint32_t name_length, struct variable** result) {
    struct variable* var;
    for (uint32_t i = generator->local_variable_count; i > 0; i--) {
        var = &generator->local_variables[i - 1];
        if (var->name_length == name_length &&
            strncmp(var->name, name, name_length) == 0) {
            *result = var;
            return true;
        }
    }

    // check for global variable , if it is not a local variable
    for (uint32_t i = generator->global_variable_count; i > 0; i--) {
        var = &generator->global_variables[i - 1];
        if (var->name_length == name_length &&
            strncmp(var->name, name, name_length) == 0) {
            *result = var;
            return true;
        }
    }
    return false;
}

static uint32_t declare_global(struct generator* generator, const char* name,
                               uint32_t name_length) {
    //
    struct variable* var;
    for (uint32_t i = generator->global_variable_count; i > 0; i--) {
        var = &generator->global_variables[i - 1];
        if (var->name_length == name_length &&
            strncmp(var->name, name, name_length) == 0) {
            return var->allocated_reg;
        }
    }

    struct variable new_variable = {
        .name = name,
        .name_length = name_length,
        .scope = SCOPE_GLOBAL,
        .scope_depth = generator->scope_depth,
        .allocated_reg = generator->global_variable_count++,
    };

    arrput(generator->global_variables, new_variable);

    return new_variable.allocated_reg;
}

static void declare_param(struct generator* generator, const char* name,
                          uint32_t name_length) {
    struct variable var;
    var.scope = SCOPE_PARAM;
    var.scope_depth = generator->scope_depth;
    var.name = name;
    var.name_length = name_length;
    var.allocated_reg = generator_allocate_register(generator);
    generator->local_variable_count++;
    arrput(generator->local_variables, var);
}

static inline void generator_switch_basic_block(struct generator* generator,
                                                size_t block_id) {
    generator->current_block_id = block_id;
}

static inline size_t generator_add_int_contant(struct generator* generator,
                                               int64_t val) {
    arrput(generator->constants, lu_value_int(val));
    return generator->constant_counter++;
}

static inline size_t generator_add_identifier(struct generator* generator,
                                              struct lu_string* id) {
    arrput(generator->identifier_table, id);
    return generator->identifier_table_size++;
}

static inline size_t generator_add_str_contant(struct generator* generator,
                                               char* data,
                                               struct span* str_span) {
    const size_t len = str_span->end - 1 - str_span->start - 1;
    char* buffer = malloc(len);
    memcpy(buffer, data, len);
    buffer[len] = '\0';
    struct lu_string* str = lu_string_new(generator->state, buffer);
    arrput(generator->constants, lu_value_object(str));
    free(buffer);
    return generator->constant_counter++;
}

static inline size_t generator_add_executable_contant(
    struct generator* generator, struct executable* executable) {
    arrput(generator->constants, lu_value_object(executable));
    return generator->constant_counter++;
}

static uint32_t generator_emit_load_constant(struct generator* generator,
                                             size_t const_index) {
    struct instruction instr = {
        .load_const =
            {
                const_index,
                generator_allocate_register(generator),
            },
        .opcode = OPCODE_LOAD_CONST,
    };
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
    return instr.load_const.destination_reg;
}

static void generator_emit_instruction(struct generator* generator,
                                       enum opcode opcode) {
    struct instruction instr = {.opcode = opcode};
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
}

static enum opcode binop_to_opcode[] = {
    OPCODE_ADD,
    OPCODE_SUB,
    OPCODE_MUL,
    OPCODE_DIV,
    OPCODE_MOD,
    OPCODE_TEST_GREATER_THAN,
    OPCODE_TEST_GREATER_THAN_EQUAL,
    OPCODE_TEST_LESS_THAN,
    OPCODE_TEST_LESS_THAN_EQUAL,

    OPCODE_TEST_EQUAL,
    OPCODE_TEST_NOT_EQUAL,
};

static enum opcode unop_to_opcode[] = {
    OPCODE_UNARY_PLUS,
    OPCODE_UNARY_MINUS,
    OPCODE_UNARY_NOT,
};

static char* extract_name_and_len(struct generator* generator,
                                  struct ast_node* node, uint32_t* out_len) {
    char* name = generator->program.source + node->span.start;
    uint32_t name_len = node->span.end - node->span.start;
    if (out_len) *out_len = name_len;
    return name;
}

static uint32_t generator_emit_binop_instruction(struct generator* generator,
                                                 enum binary_op binop,
                                                 uint32_t lhs_register_index,
                                                 uint32_t rhs_register_index) {
    const uint32_t dst_reg = generator_allocate_register(generator);

    struct instruction instr = {};
    instr.binary_op.left_reg = lhs_register_index;
    instr.binary_op.right_reg = rhs_register_index;
    instr.binary_op.result_reg = dst_reg;
    instr.opcode = binop_to_opcode[binop];
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
    return dst_reg;
}

static void generator_emit_mov_instruction(struct generator* generator,
                                           uint32_t dst_register_index,
                                           uint32_t src_register_index) {
    struct instruction instr = {
        .opcode = OPCODE_MOV,
    };
    instr.mov.src_reg = src_register_index;
    instr.mov.dest_reg = dst_register_index;
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
}

static void generator_emit_jump_instruction(struct generator* generator,
                                            uint32_t target_block_id,
                                            struct span span) {
    struct instruction instr = {
        .opcode = OPCODE_JUMP,
    };
    instr.jmp.target_offset = target_block_id;
    arrput(GET_CURRENT_BLOCK.instructions, instr);
    arrput(GET_CURRENT_BLOCK.instructions_spans, span);
}

//
static uint32_t generate_expr(struct generator* generator,
                              struct ast_node* expr);
static void generate_stmts(struct generator* generator,
                           struct ast_node** stmts);
static void generate_stmt(struct generator* generator, struct ast_node* stmt);
//
static inline void emit_instruction(struct generator* generator,
                                    struct instruction instr,
                                    struct span span) {
    arrput(GET_CURRENT_BLOCK.instructions_spans, span);
    arrput(GET_CURRENT_BLOCK.instructions, instr);
}

static inline uint32_t generate_simple_load(struct generator* generator,
                                            enum opcode op, struct span span) {
    const uint32_t dst_reg = generator_allocate_register(generator);
    struct instruction instr = {.opcode = op, .destination_reg = dst_reg};
    emit_instruction(generator, instr, span);
    return dst_reg;
}

static inline uint32_t generate_constant_load(struct generator* generator,
                                              size_t const_index,
                                              struct span span) {
    arrput(GET_CURRENT_BLOCK.instructions_spans, span);
    return generator_emit_load_constant(generator, const_index);
}

static inline uint32_t generate_int_expr(struct generator* generator,
                                         struct ast_node* expr) {
    size_t const_index =
        generator_add_int_contant(generator, expr->data.int_val);
    return generate_constant_load(generator, const_index, expr->span);
}

static inline uint32_t generate_bool_expr(struct generator* generator,
                                          struct ast_node* expr) {
    enum opcode op = expr->data.int_val ? OPCODE_LOAD_TRUE : OPCODE_LOAD_FALSE;
    return generate_simple_load(generator, op, expr->span);
}

static inline uint32_t generate_str_expr(struct generator* generator,
                                         struct ast_node* expr) {
    size_t const_index =
        generator_add_str_contant(generator, expr->data.id, &expr->span);
    return generate_constant_load(generator, const_index, expr->span);
}

static inline uint32_t generate_array_expr(struct generator* generator,
                                           struct ast_node* expr) {
    uint32_t dst_reg = generator_allocate_register(generator);

    struct instruction new_array_instr = {.opcode = OPCODE_NEW_ARRAY,
                                          .destination_reg = dst_reg};
    emit_instruction(generator, new_array_instr, expr->span);

    for (uint32_t i = 0; i < arrlen(expr->data.list); i++) {
        struct ast_node* element = expr->data.list[i];
        struct instruction append_instr = {
            .opcode = OPCODE_ARRAY_APPEND,
            .pair.fst = dst_reg,
            .pair.snd = generate_expr(generator, element)};
        emit_instruction(generator, append_instr, element->span);
    }

    return dst_reg;
}

static inline uint32_t generate_identifier_expr(struct generator* generator,
                                                struct ast_node* expr) {
    uint32_t name_len;
    char* name = extract_name_and_len(generator, expr, &name_len);

    uint32_t dst_reg = generator_allocate_register(generator);

    struct variable* var;
    if (!find_variable(generator, name, name_len, &var)) {
        char* name_copy = malloc(name_len + 1);
        memcpy(name_copy, name, name_len);
        name_copy[name_len] = '\0';

        struct lu_string* name_string =
            lu_intern_string(generator->state, name_copy);

        uint32_t name_index = generator_add_identifier(generator, name_string);

        struct instruction instr = {.opcode = OPCODE_LOAD_GLOBAL_BY_NAME,
                                    .pair.fst = name_index,
                                    .pair.snd = dst_reg};
        emit_instruction(generator, instr, expr->span);
        return instr.pair.snd;
    }

    // Local and parameter variables are accessed directly
    if (var->scope == SCOPE_LOCAL || var->scope == SCOPE_PARAM) {
        return var->allocated_reg;
    }

    // Global variables require a load instruction
    struct instruction instr = {.opcode = OPCODE_LOAD_GLOBAL_BY_INDEX,
                                .mov.dest_reg = dst_reg,
                                .mov.src_reg = var->allocated_reg};
    emit_instruction(generator, instr, expr->span);
    return dst_reg;
}

static inline uint32_t generate_unop_expr(struct generator* generator,
                                          struct ast_node* expr) {
    uint32_t dst_reg = generate_expr(generator, expr->data.unop.argument);
    struct instruction instr = {.opcode = unop_to_opcode[expr->data.unop.op],
                                .destination_reg = dst_reg};
    emit_instruction(generator, instr, expr->span);
    return dst_reg;
}

static inline uint32_t generate_logical_binop(struct generator* generator,
                                              struct ast_node* expr) {
    uint32_t lhs = generate_expr(generator, expr->data.binop.lhs);
    uint32_t dst = generator_allocate_register(generator);
    const size_t rhs_block = generator_basic_block_new(generator);
    const size_t end_block = generator_basic_block_new(generator);

    generator_emit_mov_instruction(generator, dst, lhs);
    arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);

    struct instruction branch_instr = {.opcode = OPCODE_JMP_IF,
                                       .jmp_if.condition_reg = lhs,
                                       .jmp_if.true_block_id = rhs_block,
                                       .jmp_if.false_block_id = end_block};

    if (expr->data.binop.op == OP_LOR) {
        branch_instr.jmp_if.true_block_id = end_block;
        branch_instr.jmp_if.false_block_id = rhs_block;
    }

    emit_instruction(generator, branch_instr, expr->span);

    generator_switch_basic_block(generator, rhs_block);
    uint32_t rhs = generate_expr(generator, expr->data.binop.rhs);
    generator_emit_mov_instruction(generator, dst, rhs);
    arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
    generator_emit_jump_instruction(generator, end_block, expr->span);
    arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);

    generator_switch_basic_block(generator, end_block);
    return dst;
}

static inline uint32_t generate_binop(struct generator* generator,
                                      struct ast_node* expr) {
    if (expr->data.binop.op < OP_LAND) {
        uint32_t lhs = generate_expr(generator, expr->data.binop.lhs);
        uint32_t rhs = generate_expr(generator, expr->data.binop.rhs);
        arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
        return generator_emit_binop_instruction(generator, expr->data.binop.op,
                                                lhs, rhs);
    }
    return generate_logical_binop(generator, expr);
}

static inline void generate_simple_assign(struct generator* generator,
                                          struct ast_node* lhs,
                                          uint32_t val_reg, struct span span) {
    uint32_t name_len;
    char* name = extract_name_and_len(generator, lhs, &name_len);

    struct variable* var;
    if (!find_variable(generator, name, name_len, &var)) {
        lu_raise_error(generator->state,
                       lu_string_new(generator->state, "undeclared variable"));
        return;
    }

    struct instruction instr = {.opcode = (var->scope == SCOPE_GLOBAL)
                                              ? OPCODE_STORE_GLOBAL_BY_INDEX
                                              : OPCODE_MOV,
                                .mov.dest_reg = var->allocated_reg,
                                .mov.src_reg = val_reg};
    emit_instruction(generator, instr, span);
}

static inline void generate_subscript_assign(struct generator* generator,
                                             struct ast_node* lhs,
                                             uint32_t val_reg,
                                             struct span span) {
    struct instruction instr = {
        .opcode = OPCODE_STORE_SUBSCR,
        .binary_op.left_reg = generate_expr(generator, lhs->data.pair.fst),
        .binary_op.right_reg = generate_expr(generator, lhs->data.pair.snd),
        .binary_op.result_reg = val_reg};
    emit_instruction(generator, instr, span);
}

static inline void generate_object_property_assign(struct generator* generator,
                                                   struct ast_node* lhs,
                                                   uint32_t val_reg,
                                                   struct span span) {
    uint32_t obj = generate_expr(generator, lhs->data.member_expr.object);
    char* name =
        generator->program.source + lhs->data.member_expr.property_name.start;
    uint32_t name_len = lhs->data.member_expr.property_name.end -
                        lhs->data.member_expr.property_name.start;

    char* name_copy = malloc(name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';

    struct lu_string* name_string =
        lu_intern_string(generator->state, name_copy);
    free(name_copy);
    uint32_t name_index = generator_add_identifier(generator, name_string);

    struct instruction instr = {
        .opcode = OPCODE_OBJECT_SET_PROPERTY,
        .binary_op.left_reg = name_index,
        .binary_op.right_reg = val_reg,
        .binary_op.result_reg = obj,
    };
    emit_instruction(generator, instr, span);
}

static inline uint32_t generate_assign_expr(struct generator* generator,
                                            struct ast_node* expr) {
    uint32_t val_reg = generate_expr(generator, expr->data.binop.rhs);

    switch (expr->data.binop.lhs->kind) {
        case AST_NODE_IDENTIFIER: {
            generate_simple_assign(generator, expr->data.binop.lhs, val_reg,
                                   expr->span);
            break;
        }
        case AST_NODE_COMPUTED_MEMBER_EXPR: {
            generate_subscript_assign(generator, expr->data.binop.lhs, val_reg,
                                      expr->span);
            break;
        }
        case AST_NODE_MEMBER_EXPR: {
            generate_object_property_assign(generator, expr->data.binop.lhs,
                                            val_reg, expr->span);
            break;
        }
        default:
            lu_raise_error(
                generator->state,
                lu_string_new(generator->state, "invalid assignment target"));
            break;
    }

    return val_reg;
}

// Load function by name for call expression
static inline uint32_t load_callee_by_name(struct generator* generator,
                                           struct ast_node* callee) {
    // Duplicate block of code from generate_identifier expr
    // ---------------------------------------------------
    uint32_t name_len;
    char* name = extract_name_and_len(generator, callee, &name_len);

    struct variable* var;

    uint32_t dst_reg = generator_allocate_register(generator);
    if (!find_variable(generator, name, name_len, &var)) {
        char* name_copy = malloc(name_len + 1);
        memcpy(name_copy, name, name_len);
        name_copy[name_len] = '\0';

        struct lu_string* name_string =
            lu_intern_string(generator->state, name_copy);
        uint32_t name_index = generator_add_identifier(generator, name_string);

        struct instruction instr = {.opcode = OPCODE_LOAD_GLOBAL_BY_NAME,
                                    .pair.fst = name_index,
                                    .pair.snd = dst_reg};
        emit_instruction(generator, instr, callee->span);
        free(name_copy);
        return instr.pair.snd;
    }

    if (var->scope == SCOPE_LOCAL || var->scope == SCOPE_PARAM) {
        return var->allocated_reg;
    }

    struct instruction instr = {.opcode = OPCODE_LOAD_GLOBAL_BY_INDEX,
                                .mov.dest_reg = dst_reg,
                                .mov.src_reg = var->allocated_reg};
    emit_instruction(generator, instr, callee->span);
    return dst_reg;
    // ---------------------------------------------------
}

static inline uint32_t generate_subscript_expr(struct generator* generator,
                                               struct ast_node* expr) {
    struct instruction instr = {
        .opcode = OPCODE_LOAD_SUBSCR,
        .binary_op.result_reg = generator_allocate_register(generator),
        .binary_op.left_reg = generate_expr(generator, expr->data.pair.fst),
        .binary_op.right_reg = generate_expr(generator, expr->data.pair.snd)};
    emit_instruction(generator, instr, expr->span);
    return instr.binary_op.result_reg;
}

static uint32_t generate_member_expr(struct generator* generator,
                                     struct ast_node* expr) {
    uint32_t obj = generate_expr(generator, expr->data.member_expr.object);
    uint32_t dst_reg = generator_allocate_register(generator);
    char* name =
        generator->program.source + expr->data.member_expr.property_name.start;
    uint32_t name_len = expr->data.member_expr.property_name.end -
                        expr->data.member_expr.property_name.start;

    char* name_copy = malloc(name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';

    struct lu_string* name_string =
        lu_intern_string(generator->state, name_copy);
    free(name_copy);
    uint32_t name_index = generator_add_identifier(generator, name_string);

    struct instruction get_prop_instr = {
        .opcode = OPCODE_OBJECT_GET_PROPERTY,
        .binary_op.left_reg = obj,
        .binary_op.right_reg = name_index,
        .binary_op.result_reg = dst_reg,
    };
    emit_instruction(generator, get_prop_instr, expr->span);
    return dst_reg;
}

static inline uint32_t generate_call_expr(struct generator* generator,
                                          struct ast_node* expr) {
    struct instruction call_instr;
    call_instr.opcode = OPCODE_CALL;
    call_instr.call.argc = expr->data.call.argc;
    call_instr.call.args_reg = nullptr;

    switch (expr->data.call.callee->kind) {
        case AST_NODE_IDENTIFIER: {
            call_instr.call.callee_reg =
                load_callee_by_name(generator, expr->data.call.callee);
            call_instr.call.self_reg = call_instr.call.callee_reg;
            break;
        }
        case AST_NODE_COMPUTED_MEMBER_EXPR: {
            call_instr.call.self_reg =
                generate_expr(generator, expr->data.call.callee->data.pair.fst);
            call_instr.call.callee_reg =
                generate_subscript_expr(generator, expr->data.call.callee);
            break;
        }
        case AST_NODE_MEMBER_EXPR: {
            uint32_t object_reg = generate_expr(
                generator, expr->data.call.callee->data.member_expr.object);
            call_instr.call.callee_reg =
                generate_member_expr(generator, expr->data.call.callee);
            call_instr.call.self_reg = object_reg;
            break;
        }
        default: {
            lu_raise_error(
                generator->state,
                lu_string_new(generator->state,
                              "attempt to call a non function value"));
            break;
        }
    }

    for (uint8_t i = 0; i < expr->data.call.argc; ++i) {
        uint32_t arg_reg = generate_expr(generator, expr->data.call.args[i]);
        arrput(call_instr.call.args_reg, arg_reg);
    }

    call_instr.call.ret_reg = generator_allocate_register(generator);

    emit_instruction(generator, call_instr, expr->span);
    return call_instr.call.ret_reg;
}

static inline uint32_t generate_function_expr(struct generator* generator,
                                              struct ast_node* expr) {
    // Duplicate block of code from generate_fn_decl
    //---------------------------------------------

    struct generator* fn_generator = malloc(sizeof(struct generator));
    generator_init(fn_generator, generator->program);
    fn_generator->state = generator->state;
    fn_generator->state->ir_generator = fn_generator;
    fn_generator->prev = generator;
    fn_generator->global_variables = generator->global_variables;
    fn_generator->global_variable_count = generator->global_variable_count;
    fn_generator->scope_depth = 1;

    generator = fn_generator;
    uint32_t fn_entry_block = generator_basic_block_new(generator);

    const size_t num_params = arrlen(expr->data.fn_decl.params);

    for (size_t i = 0; i < num_params; ++i) {
        struct ast_node* param = expr->data.fn_decl.params[i];
        const char* param_name = generator->program.source + param->span.start;
        uint32_t param_name_len = param->span.end - param->span.start;
        declare_param(generator, param_name, param_name_len);
    }

    struct lu_string* name_string =
        lu_intern_string(generator->state, "<anonymous>");

    generator_switch_basic_block(generator, fn_entry_block);
    generate_stmt(generator, expr->data.fn_decl.body);

    struct executable* fn_executable = generator_make_executable(generator);
    fn_executable->name = name_string;
    generator = generator->prev;
    generator->global_variable_count = fn_generator->global_variable_count;
    uint32_t executable_index =
        generator_add_executable_contant(generator, fn_executable);

    uint32_t name_index = generator_add_identifier(generator, name_string);

    struct instruction make_fn_instr;
    make_fn_instr.opcode = OPCODE_MAKE_FUNCTION;
    make_fn_instr.binary_op.result_reg = generator_allocate_register(generator);
    make_fn_instr.binary_op.left_reg = executable_index;
    make_fn_instr.binary_op.right_reg = name_index;

    emit_instruction(generator, make_fn_instr, expr->span);
    //-----------------------------------------------
    return make_fn_instr.binary_op.result_reg;
}

static uint32_t generate_object_expr(struct generator* generator,
                                     struct ast_node* expr) {
    //
    uint32_t dst_reg = generator_allocate_register(generator);

    struct instruction new_object_instr = {.opcode = OPCODE_NEW_OBJECT,
                                           .destination_reg = dst_reg};
    emit_instruction(generator, new_object_instr, expr->span);

    const size_t len = arrlen(expr->data.list);
    struct ast_node* prop;
    for (size_t i = 0; i < len; i++) {
        prop = expr->data.list[i];
        char* name =
            generator->program.source + prop->data.property.property_name.start;
        uint32_t name_len = prop->data.property.property_name.end -
                            prop->data.property.property_name.start;

        char* name_copy = malloc(name_len + 1);
        memcpy(name_copy, name, name_len);
        name_copy[name_len] = '\0';

        struct lu_string* name_string =
            lu_intern_string(generator->state, name_copy);
        free(name_copy);
        uint32_t name_index = generator_add_identifier(generator, name_string);
        uint32_t prop_value =
            generate_expr(generator, prop->data.property.value);
        struct instruction set_prop_instr = {
            .opcode = OPCODE_OBJECT_SET_PROPERTY,
            .binary_op.left_reg = name_index,
            .binary_op.right_reg = prop_value,
            .binary_op.result_reg = dst_reg,
        };
        emit_instruction(generator, set_prop_instr, prop->span);
    }

    return dst_reg;
}

static uint32_t generate_expr(struct generator* generator,
                              struct ast_node* expr) {
    switch (expr->kind) {
        case AST_NODE_SELF_EXPR: {
            return 0;
        }
        case AST_NODE_INT: {
            return generate_int_expr(generator, expr);
        }
        case AST_NODE_NONE: {
            return generate_simple_load(generator, OPCODE_LOAD_NONE,
                                        expr->span);
        }
        case AST_NODE_BOOL: {
            return generate_bool_expr(generator, expr);
        }
        case AST_NODE_STR: {
            return generate_str_expr(generator, expr);
        }
        case AST_NODE_ARRAY_EXPR: {
            return generate_array_expr(generator, expr);
        }
        case AST_NODE_IDENTIFIER: {
            return generate_identifier_expr(generator, expr);
        }
        case AST_NODE_UNOP: {
            return generate_unop_expr(generator, expr);
        }
        case AST_NODE_BINOP: {
            return generate_binop(generator, expr);
        }
        case AST_NODE_ASSIGN: {
            return generate_assign_expr(generator, expr);
        }
        case AST_NODE_CALL: {
            return generate_call_expr(generator, expr);
        }
        case AST_NODE_MEMBER_EXPR: {
            return generate_member_expr(generator, expr);
        }
        case AST_NODE_COMPUTED_MEMBER_EXPR: {
            return generate_subscript_expr(generator, expr);
        }
        case AST_NODE_FN_EXPR: {
            return generate_function_expr(generator, expr);
        }
        case AST_NODE_OBJECT_EXPR: {
            return generate_object_expr(generator, expr);
        }
        default: {
            return 0;
        }
    }
}

static inline void begin_scope(struct generator* generator) {
    generator->scope_depth++;
}

static void end_scope(struct generator* generator) {
    size_t number_of_elements_to_pop = 0;
    struct variable* var;
    for (size_t i = generator->local_variable_count; i > 0; i--) {
        var = &generator->local_variables[i - 1];
        if (var->scope == SCOPE_LOCAL &&
            var->scope_depth == generator->scope_depth) {
            number_of_elements_to_pop++;
        }
    }

    while (number_of_elements_to_pop > 0) {
        arrpop(generator->local_variables);
        number_of_elements_to_pop--;
    }
    generator->scope_depth--;
}

static inline void generate_let_decl(struct generator* generator,
                                     struct ast_node* stmt) {
    uint32_t value = generate_expr(generator, stmt->data.let_decl.value);
    struct variable* var;

    char* name =
        generator->program.source + stmt->data.let_decl.name_span.start;
    uint32_t name_len =
        stmt->data.let_decl.name_span.end - stmt->data.let_decl.name_span.start;
    declare_variable(generator, name, name_len, &var);

    struct instruction store_instr = {
        .opcode = var->scope == SCOPE_GLOBAL ? OPCODE_STORE_GLOBAL_BY_INDEX
                                             : OPCODE_MOV,
    };
    store_instr.mov.dest_reg = var->allocated_reg;
    store_instr.mov.src_reg = value;

    emit_instruction(generator, store_instr, stmt->span);
}

static inline void generate_return_stmt(struct generator* generator,
                                        struct ast_node* stmt) {
    uint32_t value = generate_expr(generator, stmt->data.node);
    struct instruction instr = {.opcode = OPCODE_RET, .destination_reg = value};
    emit_instruction(generator, instr, stmt->span);
}

static inline void generate_break_stmt(struct generator* generator,
                                       struct ast_node* stmt) {
    generator_emit_jump_instruction(
        generator,
        generator->loop_stack[arrlen(generator->loop_stack) - 1].end_block_id,
        stmt->span);
    arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
}

static inline void generate_continue_stmt(struct generator* generator,
                                          struct ast_node* stmt) {
    generator_emit_jump_instruction(
        generator,
        generator->loop_stack[arrlen(generator->loop_stack) - 1].start_block_id,
        stmt->span);
    arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
}

static inline void generate_if_stmt(struct generator* generator,
                                    struct ast_node* stmt) {
    struct ast_node* current = stmt;
    uint32_t end_block = generator_basic_block_new(generator);
    while (current && current->kind == AST_NODE_IF_STMT) {
        uint32_t true_block = generator_basic_block_new(generator);
        uint32_t false_block = generator_basic_block_new(generator);

        uint32_t condition =
            generate_expr(generator, current->data.if_stmt.test);

        struct instruction branch_instr = {
            .opcode = OPCODE_JMP_IF,
        };
        branch_instr.jmp_if.condition_reg = condition;
        branch_instr.jmp_if.true_block_id = true_block;
        branch_instr.jmp_if.false_block_id = false_block;

        emit_instruction(generator, branch_instr, current->span);

        generator_switch_basic_block(generator, true_block);
        generate_stmt(generator, current->data.if_stmt.consequent);

        generator_emit_jump_instruction(generator, end_block, current->span);

        current = current->data.if_stmt.alternate;
        generator_switch_basic_block(generator, false_block);
    }

    if (current) {
        generate_stmt(generator, current);
        generator_emit_jump_instruction(generator, end_block, current->span);
    } else {
        generator_emit_jump_instruction(generator, end_block, stmt->span);
    }

    generator_switch_basic_block(generator, end_block);
}

static inline void generate_loop_stmt(struct generator* generator,
                                      struct ast_node* stmt) {
    uint32_t start_block = generator_basic_block_new(generator);
    uint32_t end_block = generator_basic_block_new(generator);

    generator_emit_jump_instruction(generator, start_block, stmt->span);
    struct loop loop = {.start_block_id = start_block,
                        .end_block_id = end_block};
    arrput(generator->loop_stack, loop);
    generator_switch_basic_block(generator, start_block);
    generate_stmt(generator, stmt->data.node);

    generator_emit_jump_instruction(generator, start_block, stmt->span);
    arrpop(generator->loop_stack);
    generator_switch_basic_block(generator, end_block);
}

static inline void generate_while_stmt(struct generator* generator,
                                       struct ast_node* stmt) {
    uint32_t test_block = generator_basic_block_new(generator);
    uint32_t body_block = generator_basic_block_new(generator);
    uint32_t end_block = generator_basic_block_new(generator);

    struct loop loop = {.start_block_id = test_block,
                        .end_block_id = end_block};
    arrput(generator->loop_stack, loop);

    generator_emit_jump_instruction(generator, test_block,
                                    stmt->data.pair.fst->span);

    generator_switch_basic_block(generator, test_block);
    uint32_t test = generate_expr(generator, stmt->data.pair.fst);

    struct instruction branch_instr = {
        .opcode = OPCODE_JMP_IF,
    };
    branch_instr.jmp_if.condition_reg = test;
    branch_instr.jmp_if.true_block_id = body_block;
    branch_instr.jmp_if.false_block_id = end_block;

    emit_instruction(generator, branch_instr, stmt->span);

    generator_switch_basic_block(generator, body_block);
    generate_stmt(generator, stmt->data.pair.snd);

    generator_emit_jump_instruction(generator, test_block,
                                    stmt->data.pair.fst->span);

    arrpop(generator->loop_stack);
    generator_switch_basic_block(generator, end_block);
}

static void generate_for_stmt(struct generator* generator,
                              struct ast_node* stmt) {
    const struct ast_for_stmt* for_stmt = &stmt->data.for_stmt;
    uint32_t init_block = generator_basic_block_new(generator);
    uint32_t test_block = generator_basic_block_new(generator);
    uint32_t body_block = generator_basic_block_new(generator);
    uint32_t update_block = generator_basic_block_new(generator);
    uint32_t end_block = generator_basic_block_new(generator);

    struct loop loop = {.start_block_id = update_block,
                        .end_block_id = end_block};
    arrput(generator->loop_stack, loop);

    generator_emit_jump_instruction(generator, init_block,
                                    for_stmt->init->span);

    begin_scope(generator);
    generator_switch_basic_block(generator, init_block);

    generate_stmt(generator, for_stmt->init);
    generator_emit_jump_instruction(generator, test_block,
                                    for_stmt->test->span);

    generator_switch_basic_block(generator, test_block);
    uint32_t test = generate_expr(generator, for_stmt->test);

    struct instruction branch_instr = {
        .opcode = OPCODE_JMP_IF,
    };
    branch_instr.jmp_if.condition_reg = test;
    branch_instr.jmp_if.true_block_id = body_block;
    branch_instr.jmp_if.false_block_id = end_block;

    emit_instruction(generator, branch_instr, for_stmt->test->span);

    generator_switch_basic_block(generator, body_block);
    generate_stmt(generator, for_stmt->body);

    generator_emit_jump_instruction(generator, update_block,
                                    for_stmt->test->span);

    generator_switch_basic_block(generator, update_block);
    generate_expr(generator, for_stmt->update);

    generator_emit_jump_instruction(generator, test_block,
                                    for_stmt->test->span);

    arrpop(generator->loop_stack);
    end_scope(generator);
    generator_switch_basic_block(generator, end_block);
}

static inline void generate_fn_decl(struct generator* generator,
                                    struct ast_node* stmt) {
    char* name = generator->program.source + stmt->data.fn_decl.name_span.start;
    uint32_t name_len =
        stmt->data.fn_decl.name_span.end - stmt->data.fn_decl.name_span.start;
    char* name_copy = malloc(name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';

    struct lu_string* name_string =
        lu_intern_string(generator->state, name_copy);

    struct variable* var;
    declare_variable(generator, name, name_len, &var);

    struct generator* fn_generator = malloc(sizeof(struct generator));

    generator_init(fn_generator, generator->program);
    fn_generator->state = generator->state;
    fn_generator->state->ir_generator = fn_generator;
    fn_generator->prev = generator;
    fn_generator->global_variables = generator->global_variables;
    fn_generator->global_variable_count = generator->global_variable_count;
    fn_generator->scope_depth = 1;

    generator = fn_generator;
    uint32_t fn_entry_block = generator_basic_block_new(generator);

    const size_t num_params = arrlen(stmt->data.fn_decl.params);

    for (size_t i = 0; i < num_params; ++i) {
        struct ast_node* param = stmt->data.fn_decl.params[i];
        const char* param_name = generator->program.source + param->span.start;
        uint32_t param_name_len = param->span.end - param->span.start;
        declare_param(generator, param_name, param_name_len);
    }

    generator_switch_basic_block(generator, fn_entry_block);
    generate_stmt(generator, stmt->data.fn_decl.body);

    uint32_t none_reg = generator_allocate_register(generator);
    struct instruction none_instr = {.opcode = OPCODE_LOAD_NONE,
                                     .destination_reg = none_reg};
    struct instruction instr = {.opcode = OPCODE_RET,
                                .destination_reg = none_reg};
    emit_instruction(generator, none_instr, stmt->span);

    emit_instruction(generator, instr, stmt->span);

    struct executable* fn_executable = generator_make_executable(generator);
    fn_executable->name = name_string;
    generator = generator->prev;
    generator->global_variable_count = fn_generator->global_variable_count;
    uint32_t executable_index =
        generator_add_executable_contant(generator, fn_executable);

    uint32_t name_index = generator_add_identifier(generator, name_string);

    struct instruction make_fn_instr;
    make_fn_instr.opcode = OPCODE_MAKE_FUNCTION;
    make_fn_instr.binary_op.result_reg = generator_allocate_register(generator);
    make_fn_instr.binary_op.left_reg = executable_index;
    make_fn_instr.binary_op.right_reg = name_index;

    emit_instruction(generator, make_fn_instr, stmt->span);

    struct instruction store_func_instr;
    store_func_instr.opcode =
        var->scope == SCOPE_GLOBAL ? OPCODE_STORE_GLOBAL_BY_INDEX : OPCODE_MOV;
    store_func_instr.mov.src_reg = make_fn_instr.binary_op.result_reg;
    store_func_instr.mov.dest_reg = var->allocated_reg;
    emit_instruction(generator, store_func_instr, stmt->span);

    free(name_copy);
}

static void generate_stmt(struct generator* generator, struct ast_node* stmt) {
    switch (stmt->kind) {
        case AST_NODE_LET_DECL: {
            return generate_let_decl(generator, stmt);
        }
        case AST_NODE_RETURN: {
            return generate_return_stmt(generator, stmt);
        }
        case AST_NODE_EXPR_STMT: {
            generate_expr(generator, stmt->data.node);
            break;
        }
        case AST_NODE_BREAK_STMT: {
            return generate_break_stmt(generator, stmt);
        }
        case AST_NODE_CONTINUE_STMT: {
            return generate_continue_stmt(generator, stmt);
        }
        case AST_NODE_BLOCK: {
            begin_scope(generator);
            generate_stmts(generator, stmt->data.list);
            end_scope(generator);
            break;
        }
        case AST_NODE_IF_STMT: {
            return generate_if_stmt(generator, stmt);
        }
        case AST_NODE_LOOP_STMT: {
            return generate_loop_stmt(generator, stmt);
        }
        case AST_NODE_WHILE_STMT: {
            return generate_while_stmt(generator, stmt);
        }
        case AST_NODE_FOR_STMT: {
            return generate_for_stmt(generator, stmt);
        }
        case AST_NODE_FN_DECL: {
            return generate_fn_decl(generator, stmt);
        }
        default: {
            break;
        }
    }
}

static void generate_stmts(struct generator* generator,
                           struct ast_node** stmts) {
    const uint32_t nstmts = arrlen(stmts);
    for (uint32_t i = 0; i < nstmts; i++) {
        generate_stmt(generator, stmts[i]);
    }
}

struct executable* generator_generate(struct lu_istate* state,
                                      struct ast_program program) {
    // Move to arena alloc
    struct generator* generator = malloc(sizeof(struct generator));
    generator->state = state;
    generator->state->ir_generator = generator;
    generator_init(generator, program);
    size_t entry_block_id = generator_basic_block_new(generator);
    generator->current_block_id = entry_block_id;
    generate_stmts(generator, program.nodes);
    struct executable* executable = generator_make_executable(generator);
    executable->name = lu_intern_string(state, "<module>");
    return executable;
}

static void generator_basic_blocks_linearize(struct generator* generator,
                                             struct executable* executable) {
    // calculate block start offsets
    size_t* block_start_offsets =
        malloc(sizeof(size_t) * generator->block_counter);
    size_t offset = 0;
    for (size_t i = 0; i < generator->block_counter; i++) {
        struct basic_block* block = &generator->blocks[i];
        block->start_offset = offset;
        block_start_offsets[i] = offset;
        offset += arrlen(generator->blocks[i].instructions);
    }

    // calculate total instructions and allocate memory for flat instruction
    // array
    size_t total_instructions = offset;
    struct instruction* flat =
        malloc(sizeof(struct instruction) * total_instructions);
    struct span* instructions_spans =
        malloc(sizeof(struct span) * total_instructions);

    // copy each block's instruction into flat instruction array
    size_t write_index = 0;
    for (size_t i = 0; i < generator->block_counter; i++) {
        struct basic_block* blk = &generator->blocks[i];
        memcpy(&flat[write_index], blk->instructions,
               arrlen(blk->instructions) * sizeof(struct instruction));
        memcpy(&instructions_spans[write_index], blk->instructions_spans,
               arrlen(blk->instructions_spans) * sizeof(struct span));
        write_index += arrlen(blk->instructions);
    }

    // patch jump instructions
    // find a better way to do this.
    //
    // instead of doing a full loop on the instructions
    // its better to store the jump instructions inside a seperate array (like a
    // patch buffer) and patch those to avoid unnecessary iterations.
    for (size_t i = 0; i < total_instructions; i++) {
        struct instruction* instr = &flat[i];
        switch (instr->opcode) {
            case OPCODE_JUMP: {
                instr->jmp.target_offset =
                    block_start_offsets[instr->jmp.target_offset];
                break;
            }
            case OPCODE_JMP_IF: {
                instr->jmp_if.true_block_id =
                    block_start_offsets[instr->jmp_if.true_block_id];
                instr->jmp_if.false_block_id =
                    block_start_offsets[instr->jmp_if.false_block_id];
                break;
            }
            default:
                break;
        }
    }

    free(block_start_offsets);

    executable->instructions = flat;
    executable->instructions_size = total_instructions;
    executable->instructions_span = instructions_spans;
}

struct executable* generator_make_executable(struct generator* generator) {
    struct executable* executable =
        lu_object_new_sized(generator->state, sizeof(struct executable));
    executable->vtable = &lu_executable_vtable;
    executable->constants = generator->constants;
    executable->constants_size = arrlen(executable->constants);
    executable->max_register_count = generator->register_counter;
    executable->file_path = generator->program.filepath;
    executable->global_variable_count = generator->global_variable_count;
    executable->identifier_table = generator->identifier_table;
    executable->identifier_table_size = generator->identifier_table_size;
    generator_basic_blocks_linearize(generator, executable);
    return executable;
}
