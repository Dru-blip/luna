#include <stdint.h>
#include <stdlib.h>

#include "ast.h"
#include "bytecode/ir.h"
#include "operator.h"
#include "stb_ds.h"
#include "value.h"

void generator_init(struct generator* generator, struct ast_program program) {
    generator->program = program;
    generator->current_block_id = 0;
    generator->block_counter = 0;
    generator->constant_counter = 0;
    generator->register_counter = 0;
    generator->blocks = nullptr;
    generator->node = nullptr;
    generator->constants = nullptr;
    generator->instructions_span = nullptr;
}

size_t generator_basic_block_new(struct generator* generator) {
    struct basic_block block;
    block.instructions = nullptr;
    block.id = generator->block_counter++;
    arrput(generator->blocks, block);
    return block.id;
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

static inline uint32_t generator_allocate_register(
    struct generator* generator) {
    return generator->register_counter++;
}

static uint32_t generator_emit_load_constant(struct generator* generator,
                                             size_t const_index) {
    struct instruction instr = {
        .const_index = const_index,
        .register_index = generator_allocate_register(generator),
        .opcode = OPCODE_LOAD_CONST,
    };
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
    return instr.register_index;
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

    struct instruction instr = {
        .opcode = binop_to_opcode[binop],
        .r1 = lhs_register_index,
        .r2 = rhs_register_index,
        .dst = dst_reg,
    };
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
    return dst_reg;
}

static void generator_emit_mov_instruction(struct generator* generator,
                                           uint32_t dst_register_index,
                                           uint32_t src_register_index) {
    struct instruction instr = {
        .opcode = OPCODE_MOV,
        .m_dst = dst_register_index,
        .m_src = src_register_index,
    };
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
}

static void generator_emit_jump_instruction(struct generator* generator,
                                            uint32_t target_block_id) {
    struct instruction instr = {
        .opcode = OPCODE_JUMP,
        .target_block_id = target_block_id,
    };
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
            arrput(generator->instructions_span, expr->span);
            return generator_emit_load_constant(generator, const_index);
        }
        case AST_NODE_NONE: {
            const uint32_t dst_reg = generator_allocate_register(generator);
            arrput(generator->instructions_span, expr->span);
            struct instruction instr = {
                .register_index = dst_reg,
                .opcode = OPCODE_LOAD_NONE,
            };
            arrput(generator->blocks[generator->current_block_id].instructions,
                   instr);
            return dst_reg;
        }
        case AST_NODE_BOOL: {
            const uint32_t dst_reg = generator_allocate_register(generator);
            arrput(generator->instructions_span, expr->span);
            struct instruction instr = {
                .register_index = dst_reg,
                .opcode =
                    expr->data.int_val ? OPCODE_LOAD_TRUE : OPCODE_LOAD_FALSE,
            };
            arrput(generator->blocks[generator->current_block_id].instructions,
                   instr);
            return dst_reg;
        }
        case AST_NODE_STR: {
            size_t const_index = generator_add_str_contant(
                generator, expr->data.id, &expr->span);
            arrput(generator->instructions_span, expr->span);
            return generator_emit_load_constant(generator, const_index);
        }
        case AST_NODE_UNOP: {
            uint32_t dst_reg =
                generate_expr(generator, expr->data.unop.argument);
            arrput(generator->instructions_span, expr->span);
            struct instruction instr = {
                .register_index = dst_reg,
                .opcode = unop_to_opcode[expr->data.unop.op],
            };
            arrput(generator->blocks[generator->current_block_id].instructions,
                   instr);
            return dst_reg;
        }
        case AST_NODE_BINOP: {
            if (expr->data.binop.op >= OP_LAND) {
                uint32_t lhs = generate_expr(generator, expr->data.binop.lhs);
                uint32_t dst = generator_allocate_register(generator);
                const size_t rhs_block = generator_basic_block_new(generator);
                const size_t end_block = generator_basic_block_new(generator);

                generator_emit_mov_instruction(generator, dst, lhs);
                arrput(generator->instructions_span, expr->span);

                struct instruction branch_instr = {
                    .opcode = OPCODE_JMP_IF,
                    .cond = lhs,
                    .true_block_id = rhs_block,
                    .false_block_id = end_block,
                };

                if (expr->data.binop.op == OP_LOR) {
                    branch_instr.true_block_id = end_block;
                    branch_instr.false_block_id = rhs_block;
                }

                arrput(generator->instructions_span, expr->span);
                arrput(
                    generator->blocks[generator->current_block_id].instructions,
                    branch_instr);
                generator_switch_basic_block(generator, rhs_block);

                uint32_t rhs = generate_expr(generator, expr->data.binop.rhs);
                generator_emit_mov_instruction(generator, dst, rhs);
                arrput(generator->instructions_span, expr->span);
                generator_emit_jump_instruction(generator, end_block);
                arrput(generator->instructions_span, expr->span);
                generator_switch_basic_block(generator, end_block);
                return dst;
            } else {
                uint32_t lhs = generate_expr(generator, expr->data.binop.lhs);
                uint32_t rhs = generate_expr(generator, expr->data.binop.rhs);
                arrput(generator->instructions_span, expr->span);
                return generator_emit_binop_instruction(
                    generator, expr->data.binop.op, lhs, rhs);
            }
        }
        default: {
            return 0;
        }
    }
}

static void generate_stmt(struct generator* generator, struct ast_node* stmt) {
    switch (stmt->kind) {
        case AST_NODE_RETURN: {
            uint32_t value = generate_expr(generator, stmt->data.node);
            struct instruction instr = {.opcode = OPCODE_RET,
                                        .register_index = value};
            arrput(generator->instructions_span, stmt->span);
            arrput(generator->blocks[generator->current_block_id].instructions,
                   instr);
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

struct linearize_result {
    struct instruction* instructions;
    size_t size;
};

static struct linearize_result generator_basic_blocks_linearize(
    struct generator* generator) {
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

    // copy each block's instruction into flat instruction array
    size_t write_index = 0;
    for (size_t i = 0; i < generator->block_counter; i++) {
        struct basic_block* blk = &generator->blocks[i];
        memcpy(&flat[write_index], blk->instructions,
               arrlen(blk->instructions) * sizeof(struct instruction));
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
                instr->target_block_id =
                    block_start_offsets[instr->target_block_id];
                break;
            }
            case OPCODE_JMP_IF: {
                instr->true_block_id =
                    block_start_offsets[instr->true_block_id];
                instr->false_block_id =
                    block_start_offsets[instr->false_block_id];
                break;
            }
            default:
                break;
        }
    }

    free(block_start_offsets);

    return (struct linearize_result){.instructions = flat,
                                     .size = total_instructions};
}

struct exectuable* generator_make_executable(struct generator* generator) {
    struct exectuable* executable = malloc(sizeof(struct exectuable));
    executable->constants = generator->constants;
    executable->constants_size = arrlen(executable->constants);
    executable->max_register_count = generator->register_counter;
    executable->file_path = generator->program.filepath;
    executable->instructions_span = generator->instructions_span;
    struct linearize_result result =
        generator_basic_blocks_linearize(generator);
    executable->instructions = result.instructions;
    executable->instructions_size = result.size;
    return executable;
}
