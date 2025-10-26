#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "bytecode/ir.h"
#include "operator.h"
#include "stb_ds.h"
#include "value.h"

#define GET_CURRENT_BLOCK generator->blocks[generator->current_block_id]

void generator_init(struct generator* generator, struct ast_program program) {
    generator->program = program;
    generator->current_block_id = 0;
    generator->block_counter = 0;
    generator->constant_counter = 0;
    generator->register_counter = 0;
    generator->blocks = nullptr;
    generator->node = nullptr;
    generator->constants = nullptr;
    generator->local_variables = nullptr;
    generator->global_variables = nullptr;

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
        case AST_NODE_BLOCK: {
            begin_scope(generator);
            generate_stmts(generator, stmt->data.list);
            end_scope(generator);
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

struct exectuable* generator_generate(struct lu_istate* state,
                                      struct ast_program program) {
    struct generator generator;
    generator.state = state;
    generator_init(&generator, program);
    size_t entry_block_id = generator_basic_block_new(&generator);
    generator.current_block_id = entry_block_id;
    generate_stmts(&generator, program.nodes);

    return generator_make_executable(&generator);
}

static void generator_basic_blocks_linearize(struct generator* generator,
                                             struct exectuable* executable) {
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

struct exectuable* generator_make_executable(struct generator* generator) {
    struct exectuable* executable = malloc(sizeof(struct exectuable));
    executable->constants = generator->constants;
    executable->constants_size = arrlen(executable->constants);
    executable->max_register_count = generator->register_counter;
    executable->file_path = generator->program.filepath;
    executable->global_variable_count = generator->global_variable_count;
    generator_basic_blocks_linearize(generator, executable);
    return executable;
}
