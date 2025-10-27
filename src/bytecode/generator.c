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
#include "value.h"

#define GET_CURRENT_BLOCK generator->blocks[generator->current_block_id]

static void executable_finalize(struct lu_object* obj) {
    lu_object_get_default_vtable()->finalize(obj);
}

static void executable_visit(struct lu_object* obj, struct lu_objectset* set) {
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
    generator->register_counter = 0;
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
                                            uint32_t target_block_id) {
    struct instruction instr = {
        .opcode = OPCODE_JUMP,
    };
    instr.jmp.target_offset = target_block_id;
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
}

static uint32_t generate_expr(struct generator* generator,
                              struct ast_node* expr);

static uint32_t generate_expr(struct generator* generator,
                              struct ast_node* expr) {
    switch (expr->kind) {
        case AST_NODE_INT: {
            size_t const_index =
                generator_add_int_contant(generator, expr->data.int_val);
            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            return generator_emit_load_constant(generator, const_index);
        }
        case AST_NODE_NONE: {
            const uint32_t dst_reg = generator_allocate_register(generator);
            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            struct instruction instr = {
                .opcode = OPCODE_LOAD_NONE,
            };
            instr.destination_reg = dst_reg;
            arrput(GET_CURRENT_BLOCK.instructions, instr);
            return dst_reg;
        }
        case AST_NODE_BOOL: {
            const uint32_t dst_reg = generator_allocate_register(generator);
            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            struct instruction instr = {
                .opcode =
                    expr->data.int_val ? OPCODE_LOAD_TRUE : OPCODE_LOAD_FALSE,
            };
            instr.destination_reg = dst_reg;
            arrput(GET_CURRENT_BLOCK.instructions, instr);
            return dst_reg;
        }
        case AST_NODE_STR: {
            size_t const_index = generator_add_str_contant(
                generator, expr->data.id, &expr->span);
            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            return generator_emit_load_constant(generator, const_index);
        }
        case AST_NODE_IDENTIFIER: {
            char* name = generator->program.source + expr->span.start;
            uint32_t name_len = expr->span.end - expr->span.start;
            struct variable* var;
            if (!find_variable(generator, name, name_len, &var)) {
                // raise error for undeclared variables
            }
            if (var->scope == SCOPE_LOCAL) {
                return var->allocated_reg;
            }
            uint32_t dst_reg = generator_allocate_register(generator);
            struct instruction instr = {
                .opcode = OPCODE_LOAD_GLOBAL_BY_INDEX,
            };

            instr.mov.dest_reg = dst_reg;
            instr.mov.src_reg = var->allocated_reg;

            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            arrput(GET_CURRENT_BLOCK.instructions, instr);
            return dst_reg;
        }
        case AST_NODE_UNOP: {
            uint32_t dst_reg =
                generate_expr(generator, expr->data.unop.argument);
            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            struct instruction instr = {
                .opcode = unop_to_opcode[expr->data.unop.op],
            };
            instr.destination_reg = dst_reg;
            arrput(GET_CURRENT_BLOCK.instructions, instr);
            return dst_reg;
        }
        case AST_NODE_BINOP: {
            if (expr->data.binop.op < OP_LAND) {
                uint32_t lhs = generate_expr(generator, expr->data.binop.lhs);
                uint32_t rhs = generate_expr(generator, expr->data.binop.rhs);
                arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
                return generator_emit_binop_instruction(
                    generator, expr->data.binop.op, lhs, rhs);
            }
            uint32_t lhs = generate_expr(generator, expr->data.binop.lhs);
            uint32_t dst = generator_allocate_register(generator);
            const size_t rhs_block = generator_basic_block_new(generator);
            const size_t end_block = generator_basic_block_new(generator);

            generator_emit_mov_instruction(generator, dst, lhs);
            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);

            struct instruction branch_instr = {
                .opcode = OPCODE_JMP_IF,
            };

            branch_instr.jmp_if.condition_reg = lhs;
            branch_instr.jmp_if.true_block_id = rhs_block;
            branch_instr.jmp_if.false_block_id = end_block;

            if (expr->data.binop.op == OP_LOR) {
                branch_instr.jmp_if.true_block_id = end_block;
                branch_instr.jmp_if.false_block_id = rhs_block;
            }

            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            arrput(GET_CURRENT_BLOCK.instructions, branch_instr);
            generator_switch_basic_block(generator, rhs_block);

            uint32_t rhs = generate_expr(generator, expr->data.binop.rhs);
            generator_emit_mov_instruction(generator, dst, rhs);
            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            generator_emit_jump_instruction(generator, end_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            generator_switch_basic_block(generator, end_block);
            return dst;
        }
        case AST_NODE_ASSIGN: {
            uint32_t val_reg = generate_expr(generator, expr->data.binop.rhs);
            switch (expr->data.binop.lhs->kind) {
                case AST_NODE_IDENTIFIER: {
                    char* name = generator->program.source +
                                 expr->data.binop.lhs->span.start;
                    uint32_t name_len = expr->data.binop.lhs->span.end -
                                        expr->data.binop.lhs->span.start;
                    struct variable* var;
                    if (!find_variable(generator, name, name_len, &var)) {
                        // TODO: raise error for undeclared variables
                    }

                    struct instruction instr;
                    instr.opcode = var->scope == SCOPE_GLOBAL
                                       ? OPCODE_STORE_GLOBAL_BY_INDEX
                                       : OPCODE_MOV;
                    instr.mov.dest_reg = var->allocated_reg;
                    instr.mov.src_reg = val_reg;

                    arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
                    arrput(GET_CURRENT_BLOCK.instructions, instr);
                    break;
                }
                default: {
                    // TODO: raise invalid target assignment error
                    break;
                }
            }

            return val_reg;
        }
        case AST_NODE_CALL: {
            struct instruction call_instr;
            call_instr.opcode = OPCODE_CALL;
            call_instr.call.argc = expr->data.call.argc;
            call_instr.call.args_start_reg = 0;

            switch (expr->data.call.callee->kind) {
                case AST_NODE_IDENTIFIER: {
                    char* name = generator->program.source +
                                 expr->data.call.callee->span.start;
                    uint32_t name_len = expr->data.call.callee->span.end -
                                        expr->data.call.callee->span.start;

                    char* name_copy = malloc(name_len + 1);
                    memcpy(name_copy, name, name_len);
                    name_copy[name_len] = '\0';

                    struct lu_string* name_string =
                        lu_intern_string(generator->state, name_copy);

                    uint32_t name_index =
                        generator_add_identifier(generator, name_string);

                    struct instruction calle_instr;
                    calle_instr.opcode = OPCODE_LOAD_GLOBAL_BY_NAME;
                    calle_instr.pair.fst = name_index;
                    calle_instr.pair.snd =generator_allocate_register(generator);
                    arrput(GET_CURRENT_BLOCK.instructions, calle_instr);
                    arrput(GET_CURRENT_BLOCK.instructions_spans,
                           expr->data.call.callee->span);
                    call_instr.call.callee_reg = calle_instr.pair.snd;
                    break;
                }
                default: {
                    // TODO: raise uncallable expression error
                    break;
                }
            }

            call_instr.call.ret_reg = generator_allocate_register(generator);

            arrput(GET_CURRENT_BLOCK.instructions_spans, expr->span);
            arrput(GET_CURRENT_BLOCK.instructions, call_instr);

            return call_instr.call.ret_reg;
        }
        default: {
            return 0;
        }
    }
}

static void begin_scope(struct generator* generator) {
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

static void generate_stmts(struct generator* generator,
                           struct ast_node** stmts);

static void generate_stmt(struct generator* generator, struct ast_node* stmt) {
    switch (stmt->kind) {
        case AST_NODE_LET_DECL: {
            uint32_t value =
                generate_expr(generator, stmt->data.let_decl.value);
            struct variable* var;
            char* name =
                generator->program.source + stmt->data.let_decl.name_span.start;
            uint32_t name_len = stmt->data.let_decl.name_span.end -
                                stmt->data.let_decl.name_span.start;
            declare_variable(generator, name, name_len, &var);

            struct instruction store_instr = {
                .opcode = var->scope == SCOPE_GLOBAL
                              ? OPCODE_STORE_GLOBAL_BY_INDEX
                              : OPCODE_MOV,
            };
            store_instr.mov.dest_reg = var->allocated_reg;
            store_instr.mov.src_reg = value;
            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            arrput(GET_CURRENT_BLOCK.instructions, store_instr);
            break;
        }
        case AST_NODE_RETURN: {
            uint32_t value = generate_expr(generator, stmt->data.node);
            struct instruction instr = {.opcode = OPCODE_RET};
            instr.destination_reg = value;
            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            arrput(GET_CURRENT_BLOCK.instructions, instr);
            break;
        }
        case AST_NODE_EXPR_STMT: {
            generate_expr(generator, stmt->data.node);
            break;
        }
        case AST_NODE_BREAK_STMT: {
            generator_emit_jump_instruction(
                generator,
                generator->loop_stack[arrlen(generator->loop_stack) - 1]
                    .end_block_id);
            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            break;
        }
        case AST_NODE_CONTINUE_STMT: {
            generator_emit_jump_instruction(
                generator,
                generator->loop_stack[arrlen(generator->loop_stack) - 1]
                    .start_block_id);
            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            break;
        }
        case AST_NODE_BLOCK: {
            begin_scope(generator);
            generate_stmts(generator, stmt->data.list);
            end_scope(generator);
            break;
        }
        case AST_NODE_IF_STMT: {
            uint32_t true_block = generator_basic_block_new(generator);
            uint32_t false_block = generator_basic_block_new(generator);
            uint32_t end_block = stmt->data.if_stmt.alternate
                                     ? generator_basic_block_new(generator)
                                     : false_block;
            uint32_t condition =
                generate_expr(generator, stmt->data.if_stmt.test);

            struct instruction branch_instr = {
                .opcode = OPCODE_JMP_IF,
            };
            branch_instr.jmp_if.condition_reg = condition;
            branch_instr.jmp_if.true_block_id = true_block;
            branch_instr.jmp_if.false_block_id = false_block;

            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            arrput(GET_CURRENT_BLOCK.instructions, branch_instr);

            generator_switch_basic_block(generator, true_block);
            generate_stmt(generator, stmt->data.if_stmt.consequent);

            generator_emit_jump_instruction(generator, end_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);

            if (stmt->data.if_stmt.alternate) {
                generator_switch_basic_block(generator, false_block);
                generate_stmt(generator, stmt->data.if_stmt.alternate);
                generator_emit_jump_instruction(generator, end_block);
                arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            }

            generator_switch_basic_block(generator, end_block);
            break;
        }
        case AST_NODE_LOOP_STMT: {
            uint32_t start_block = generator_basic_block_new(generator);
            uint32_t end_block = generator_basic_block_new(generator);

            generator_emit_jump_instruction(generator, start_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            struct loop loop = {.start_block_id = start_block,
                                .end_block_id = end_block};
            arrput(generator->loop_stack, loop);
            generator_switch_basic_block(generator, start_block);
            generate_stmt(generator, stmt->data.node);

            generator_emit_jump_instruction(generator, start_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            arrpop(generator->loop_stack);
            generator_switch_basic_block(generator, end_block);
            break;
        }
        case AST_NODE_WHILE_STMT: {
            uint32_t test_block = generator_basic_block_new(generator);
            uint32_t body_block = generator_basic_block_new(generator);
            uint32_t end_block = generator_basic_block_new(generator);

            struct loop loop = {.start_block_id = test_block,
                                .end_block_id = end_block};
            arrput(generator->loop_stack, loop);

            generator_emit_jump_instruction(generator, test_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans,
                   stmt->data.pair.fst->span);

            generator_switch_basic_block(generator, test_block);
            uint32_t test = generate_expr(generator, stmt->data.pair.fst);

            struct instruction branch_instr = {
                .opcode = OPCODE_JMP_IF,
            };
            branch_instr.jmp_if.condition_reg = test;
            branch_instr.jmp_if.true_block_id = body_block;
            branch_instr.jmp_if.false_block_id = end_block;

            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            arrput(GET_CURRENT_BLOCK.instructions, branch_instr);

            generator_switch_basic_block(generator, body_block);
            generate_stmt(generator, stmt->data.pair.snd);

            generator_emit_jump_instruction(generator, test_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans,
                   stmt->data.pair.fst->span);

            arrpop(generator->loop_stack);
            generator_switch_basic_block(generator, end_block);
            break;
        }
        case AST_NODE_FOR_STMT: {
            const struct ast_for_stmt* for_stmt = &stmt->data.for_stmt;
            uint32_t init_block = generator_basic_block_new(generator);
            uint32_t test_block = generator_basic_block_new(generator);
            uint32_t body_block = generator_basic_block_new(generator);
            uint32_t update_block = generator_basic_block_new(generator);
            uint32_t end_block = generator_basic_block_new(generator);

            struct loop loop = {.start_block_id = update_block,
                                .end_block_id = end_block};
            arrput(generator->loop_stack, loop);

            generator_emit_jump_instruction(generator, init_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans, for_stmt->init->span);

            begin_scope(generator);
            generator_switch_basic_block(generator, init_block);

            generate_stmt(generator, for_stmt->init);
            generator_emit_jump_instruction(generator, test_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans, for_stmt->test->span);

            generator_switch_basic_block(generator, test_block);
            uint32_t test = generate_expr(generator, for_stmt->test);

            struct instruction branch_instr = {
                .opcode = OPCODE_JMP_IF,
            };
            branch_instr.jmp_if.condition_reg = test;
            branch_instr.jmp_if.true_block_id = body_block;
            branch_instr.jmp_if.false_block_id = end_block;

            arrput(GET_CURRENT_BLOCK.instructions_spans, for_stmt->test->span);
            arrput(GET_CURRENT_BLOCK.instructions, branch_instr);

            generator_switch_basic_block(generator, body_block);
            generate_stmt(generator, for_stmt->body);

            generator_emit_jump_instruction(generator, update_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans, for_stmt->test->span);

            generator_switch_basic_block(generator, update_block);
            generate_expr(generator, for_stmt->update);

            generator_emit_jump_instruction(generator, test_block);
            arrput(GET_CURRENT_BLOCK.instructions_spans, for_stmt->test->span);

            arrpop(generator->loop_stack);
            end_scope(generator);
            generator_switch_basic_block(generator, end_block);
            break;
        }
        case AST_NODE_FN_DECL: {
            struct generator* fn_generator = malloc(sizeof(struct generator));

            generator_init(fn_generator, generator->program);
            fn_generator->state = generator->state;
            fn_generator->state->ir_generator = fn_generator;
            fn_generator->prev = generator;
            fn_generator->global_variables = generator->global_variables;
            fn_generator->global_variable_count =
                generator->global_variable_count;

            generator = fn_generator;
            uint32_t fn_entry_block = generator_basic_block_new(generator);
            generator_switch_basic_block(generator, fn_entry_block);
            generate_stmt(generator, stmt->data.fn_decl.body);
            struct executable* fn_executable =
                generator_make_executable(generator);

            generator = generator->prev;
            generator->global_variable_count =
                fn_generator->global_variable_count;
            uint32_t executable_index =
                generator_add_executable_contant(generator, fn_executable);

            char* name =
                generator->program.source + stmt->data.fn_decl.name_span.start;
            uint32_t name_len = stmt->data.fn_decl.name_span.end -
                                stmt->data.fn_decl.name_span.start;
            char* name_copy = malloc(name_len + 1);
            memcpy(name_copy, name, name_len);
            name_copy[name_len] = '\0';

            struct lu_string* name_string =
                lu_intern_string(generator->state, name_copy);

            uint32_t name_index =
                generator_add_identifier(generator, name_string);

            struct instruction make_fn_instr;
            make_fn_instr.opcode = OPCODE_MAKE_FUNCTION;
            make_fn_instr.binary_op.result_reg =
                generator_allocate_register(generator);
            make_fn_instr.binary_op.left_reg = executable_index;
            make_fn_instr.binary_op.right_reg = name_index;

            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            arrput(GET_CURRENT_BLOCK.instructions, make_fn_instr);

            struct instruction store_func_instr;
            store_func_instr.opcode = OPCODE_STORE_GLOBAL_BY_NAME;
            store_func_instr.pair.fst = make_fn_instr.binary_op.result_reg;
            store_func_instr.pair.snd = name_index;

            arrput(GET_CURRENT_BLOCK.instructions_spans, stmt->span);
            arrput(GET_CURRENT_BLOCK.instructions, store_func_instr);

            free(name_copy);
            break;
        }
        default: {
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
    return generator_make_executable(generator);
}

///

struct instruction_array {
    struct instruction* data;
    size_t len;
    size_t cap;
};

void instruction_array_init(struct instruction_array* arr, size_t cap) {
    arr->cap = cap;
    arr->data = malloc(sizeof(struct instruction) * arr->cap);
    arr->len = 0;
}

size_t instruction_array_len(struct instruction_array* arr) {
    return arr ? arr->len : 0;
}

void instruction_array_put(struct instruction_array* arr,
                           struct instruction* item) {
    if (arr->len >= arr->cap) {
        size_t new_cap = arr->cap * 2;
        struct instruction* new_data =
            realloc(arr->data, sizeof(struct instruction) * new_cap);
        arr->data = new_data;
        arr->cap = new_cap;
    }

    arr->data[arr->len++] = *item;
}

void instruction_array_copy(struct instruction_array* arr,
                            struct instruction* src, size_t count) {
    size_t needed = arr->len + count;

    if (needed > arr->cap) {
        struct instruction* new_data =
            realloc(arr->data, sizeof(struct instruction) * needed);
        arr->data = new_data;
        arr->cap = needed;
    }

    memcpy(arr->data + arr->len, src, sizeof(struct instruction) * count);
    arr->len += count;
}

// static void generator_cfg_linearize(struct generator* generator,
//                                     struct executable* executable) {
//     //
//     size_t* block_start_offsets =
//         malloc(sizeof(size_t) * generator->block_counter);
//     size_t offset = 0;
//     struct basic_block** stack = nullptr;
//     assert(generator->block_counter > 0);
//     arrput(stack, &generator->blocks[0]);

//     struct instruction_array inst_arr;
//     instruction_array_init(&inst_arr,
//                            arrlen(generator->blocks[0].instructions));

//     while (arrlen(stack) > 0) {
//         struct basic_block* curr_block = arrpop(stack);
//         if (curr_block->visited) {
//             continue;
//         }
//         block_start_offsets[curr_block->id] = offset;
//         curr_block->start_offset = offset;
//         curr_block->visited = true;

//         instruction_array_copy(&inst_arr, curr_block->instructions,
//                                arrlen(curr_block->instructions));

//         offset += arrlen(curr_block->instructions);
//         struct instruction* terminator =
//             &curr_block->instructions[arrlen(curr_block->instructions) - 1];
//         assert(terminator->opcode == OPCODE_JMP_IF ||
//                terminator->opcode == OPCODE_JUMP);

//         if (terminator->opcode == OPCODE_JUMP &&
//             !generator->blocks[terminator->jmp.target_offset].visited) {
//             arrput(stack, &generator->blocks[terminator->jmp.target_offset]);
//         } else {
//             if
//             (!generator->blocks[terminator->jmp_if.false_block_id].visited) {
//                 arrput(stack,
//                        &generator->blocks[terminator->jmp_if.false_block_id]);
//             }
//             if (!generator->blocks[terminator->jmp_if.true_block_id].visited)
//             {
//                 arrput(stack,
//                        &generator->blocks[terminator->jmp_if.true_block_id]);
//             }
//         }
//     }

//     struct instruction* instructions = inst_arr.data;

//     for (size_t i = 0; i < offset; i++) {
//         struct instruction* instr = &instructions[i];
//         switch (instr->opcode) {
//             case OPCODE_JUMP: {
//                 instr->jmp.target_offset =
//                     block_start_offsets[instr->jmp.target_offset];
//                 break;
//             }
//             case OPCODE_JMP_IF: {
//                 instr->jmp_if.true_block_id =
//                     block_start_offsets[instr->jmp_if.true_block_id];
//                 instr->jmp_if.false_block_id =
//                     block_start_offsets[instr->jmp_if.false_block_id];
//                 break;
//             }
//             default:
//                 break;
//         }
//     }

//     free(block_start_offsets);

//     executable->instructions = instructions;
//     executable->instructions_size = inst_arr.len;
//     // executable->instructions_span = ;
// }

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
