#include "bytecode/ir.h"

#include <stdio.h>

static const char* opcode_names[] = {
    "load_smi",    // OPCODE_LOAD_SMI
    "load_const",  // OPCODE_LOAD_CONST
    "load_none",   // OPCODE_LOAD_NONE
    "load_true",   // OPCODE_LOAD_TRUE
    "load_false",  // OPCODE_LOAD_FALSE
    "mov",         // OPCODE_MOV

    "add",  // OPCODE_ADD
    "sub",  // OPCODE_SUB
    "mul",  // OPCODE_MUL
    "div",  // OPCODE_DIV
    "mod",  // OPCODE_MOD

    "test_gt",   // OPCODE_TEST_GREATER_THAN
    "test_gte",  // OPCODE_TEST_GREATER_THAN_EQUAL
    "test_lt",   // OPCODE_TEST_LESS_THAN
    "test_lte",  // OPCODE_TEST_LESS_THAN_EQUAL
    "test_eq",   // OPCODE_TEST_EQUAL
    "test_neq",  // OPCODE_TEST_NOT_EQUAL

    "shift_left",   // OPCODE_SHIFT_LEFT
    "shift_right",  // OPCODE_SHIFT_RIGHT

    "logic_and",  // OPCODE_LOGIC_AND
    "logic_or",   // OPCODE_LOGIC_OR

    "unary_neg",  // OPCODE_UNARY_MINUS
    "unary_pos",  // OPCODE_UNARY_PLUS
    "unary_not",  // OPCODE_UNARY_NOT

    "jump",     // OPCODE_JUMP
    "jump_if",  // OPCODE_JMP_IF
    "ret",      // OPCODE_RET
};

void print_executable(struct exectuable* executable) {
    printf("code:\n");
    for (int i = 0; i < executable->instructions_size; i++) {
        struct instruction* instr = &executable->instructions[i];
        printf("\t%s \n", opcode_names[instr->opcode]);
    }
}
