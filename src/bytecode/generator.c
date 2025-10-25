#include <stdint.h>

#include "ast.h"
#include "bytecode/ir.h"
#include "stb_ds.h"
#include "value.h"

void generator_init(struct generator* generator, struct ast_program program) {
    generator->current_block_id = 0;
    generator->blocks = nullptr;
    generator->block_counter = 0;
    generator->constant_counter = 0;
    generator->node = nullptr;
    generator->constants = nullptr;
    generator->program = program;
}

size_t generator_basic_block_new(struct generator* generator) {
    struct basic_block block;
    block.instructions = nullptr;
    block.id = generator->block_counter++;
    arrput(generator->blocks, block);
    return block.id;
}

static size_t generator_add_int_contant(struct generator* generator,
                                        int64_t val) {
    //
    arrput(generator->constants, lu_value_int(val));
    return generator->constant_counter++;
}

static void generator_emit_load_constant(struct generator* generator,
                                         size_t const_index) {
    //
    struct instruction instr = {.const_index = const_index,
                                .opcode = OPCODE_LOAD_CONST};
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
}

static void generator_emit_instruction(struct generator* generator,
                                       enum opcode opcode) {
    //
    struct instruction instr = {.opcode = opcode};
    arrput(generator->blocks[generator->current_block_id].instructions, instr);
}

static void generate_expr(struct generator* generator, struct ast_node* expr) {
    switch (expr->kind) {
        case AST_NODE_INT: {
            size_t const_index =
                generator_add_int_contant(generator, expr->data.int_val);
            generator_emit_load_constant(generator, const_index);
            break;
        }
        default: {
        }
    }
}

static void generate_stmt(struct generator* generator, struct ast_node* stmt) {
    switch (stmt->kind) {
        case AST_NODE_RETURN: {
            generate_expr(generator, stmt->data.node);
            generator_emit_instruction(generator, OPCODE_RET);
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

struct exectuable* generator_generate(struct ast_program program) {
    struct generator generator;
    generator_init(&generator, program);
    size_t entry_block_id = generator_basic_block_new(&generator);
    generator.current_block_id = entry_block_id;
    generate_stmts(&generator, program.nodes);

    return generator_make_executable(&generator);
}

struct exectuable* generator_make_executable(struct generator* generator) {
    struct exectuable* executable = malloc(sizeof(struct exectuable));
    executable->instructions = generator->blocks[0].instructions;
    executable->instructions_size = arrlen(executable->instructions);
    executable->constants = generator->constants;
    executable->constants_size = arrlen(executable->constants);
    return executable;
}
