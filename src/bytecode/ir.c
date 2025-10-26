#include "bytecode/ir.h"

#include <stdio.h>

static const char* opcode_names[] = {
    "LoadSmi",    // OPCODE_LOAD_SMI
    "LoadConst",  // OPCODE_LOAD_CONST
    "LoadNone",   // OPCODE_LOAD_NONE
    "LoadTrue",   // OPCODE_LOAD_TRUE
    "LoadFalse",  // OPCODE_LOAD_FALSE

    "LoadGlobalByIndex",   // OPCODE_LOAD_GLOBAL_BY_INDEX
    "LoadGlobalByName",    // OPCODE_LOAD_GLOBAL_BY_NAME
    "StoreGlobalByIndex",  // OPCODE_STORE_GLOBAL_BY_INDEX
    "StoreGlobalByName",   // OPCODE_STORE_GLOBAL_BY_NAME
    "StoreLocal",          // OPCODE_STORE_LOCAL
    "LoadLocal",           // OPCODE_LOAD_LOCAL

    "Mov",  // OPCODE_MOV

    "Add",  // OPCODE_ADD
    "Sub",  // OPCODE_SUB
    "Mul",  // OPCODE_MUL
    "Div",  // OPCODE_DIV
    "Mod",  // OPCODE_MOD

    "TestGt",   // OPCODE_TEST_GREATER_THAN
    "TestGte",  // OPCODE_TEST_GREATER_THAN_EQUAL
    "TestLt",   // OPCODE_TEST_LESS_THAN
    "TestLte",  // OPCODE_TEST_LESS_THAN_EQUAL
    "TestEq",   // OPCODE_TEST_EQUAL
    "TestNeq",  // OPCODE_TEST_NOT_EQUAL

    "ShiftLeft",   // OPCODE_SHIFT_LEFT
    "ShiftRight",  // OPCODE_SHIFT_RIGHT

    "UnaryNeg",  // OPCODE_UNARY_MINUS
    "UnaryPos",  // OPCODE_UNARY_PLUS
    "UnaryNot",  // OPCODE_UNARY_NOT

    "Jump",    // OPCODE_JUMP
    "JumpIf",  // OPCODE_JMP_IF
    "Ret",     // OPCODE_RET
};

void print_executable(struct exectuable* executable) {
    printf("Executable (%s):\n",
           executable->file_path ? executable->file_path : "unknown file");
    printf("Max registers : %u\n", executable->max_register_count);
    printf("Constants (%zu)\n", executable->constants_size);
    // for (size_t i = 0; i < executable->constants_size; i++) {
    //     printf("  [%zu] ", i);
    //     lu_value_print(
    //         &executable->constants[i]);  // assume you have a print function
    //     printf("\n");
    // }

    printf("\nInstructions:\n");
    for (size_t i = 0; i < executable->instructions_size; i++) {
        struct instruction* instr = &executable->instructions[i];
        printf("%04zu: %s ", i, opcode_names[instr->opcode]);

        switch (instr->opcode) {
            case OPCODE_LOAD_CONST: {
                printf("r%u [const %u]", instr->load_const.destination_reg,
                       instr->load_const.constant_index);
                break;
            }
            case OPCODE_LOAD_NONE: {
                printf("r%u", instr->destination_reg);
                break;
            }
            case OPCODE_LOAD_TRUE: {
                printf("r%u", instr->destination_reg);
                break;
            }
            case OPCODE_LOAD_FALSE: {
                printf("r%u", instr->destination_reg);
                break;
            }
            case OPCODE_STORE_GLOBAL_BY_INDEX:
            case OPCODE_LOAD_GLOBAL_BY_INDEX: {
                printf("r%u, global[%u]", instr->mov.dest_reg,
                       instr->mov.src_reg);
                break;
            }
            case OPCODE_STORE_GLOBAL_BY_NAME:
            case OPCODE_LOAD_GLOBAL_BY_NAME: {
                printf("r%u, name_index %u", instr->destination_reg,
                       instr->load_const.constant_index);
                break;
            }
            case OPCODE_STORE_LOCAL:
            case OPCODE_LOAD_LOCAL: {
                printf("r%u", instr->destination_reg);
                break;
            }
            case OPCODE_MOV: {
                printf("r%u <- r%u", instr->mov.dest_reg, instr->mov.src_reg);
                break;
            }
            case OPCODE_ADD:
            case OPCODE_SUB:
            case OPCODE_MUL:
            case OPCODE_DIV:
            case OPCODE_MOD: {
                printf("r%u <- r%u , r%u", instr->binary_op.result_reg,
                       instr->binary_op.left_reg, instr->binary_op.right_reg);
                break;
            }
            case OPCODE_TEST_GREATER_THAN:
            case OPCODE_TEST_GREATER_THAN_EQUAL:
            case OPCODE_TEST_LESS_THAN:
            case OPCODE_TEST_LESS_THAN_EQUAL:
            case OPCODE_TEST_EQUAL:
            case OPCODE_TEST_NOT_EQUAL: {
                printf("r%u <- r%u cmp r%u", instr->binary_op.result_reg,
                       instr->binary_op.left_reg, instr->binary_op.right_reg);
                break;
            }
            case OPCODE_SHIFT_LEFT:
            case OPCODE_SHIFT_RIGHT: {
                printf("r%u <- r%u shift r%u", instr->binary_op.result_reg,
                       instr->binary_op.left_reg, instr->binary_op.right_reg);
                break;
            }
            case OPCODE_UNARY_MINUS:
            case OPCODE_UNARY_PLUS:
            case OPCODE_UNARY_NOT: {
                printf("r%u <- op r%u", instr->destination_reg,
                       instr->destination_reg);
                break;
            }
            case OPCODE_JUMP: {
                printf("%u", instr->jmp.target_offset);
                break;
            }
            case OPCODE_JMP_IF: {
                printf("r%u goto %u else goto %u", instr->jmp_if.condition_reg,
                       instr->jmp_if.true_block_id,
                       instr->jmp_if.false_block_id);
                break;
            }
            case OPCODE_RET: {
                printf("r%u", instr->destination_reg);
                break;
            }
            default: {
                printf("unknown opcode");
                break;
            }
        }

        printf("\n");
    }
}
