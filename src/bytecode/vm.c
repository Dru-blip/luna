#include "bytecode/vm.h"

#include <stdlib.h>

#include "bytecode/interpreter.h"
#include "bytecode/ir.h"
#include "stb_ds.h"
// #include "value.h"
#include "bytecode/vm_ops.h"

struct lu_vm* lu_vm_new(struct lu_istate* istate) {
    struct lu_vm* vm = malloc(sizeof(struct lu_vm));
    vm->records = nullptr;
    vm->rp = 0;
    vm->istate = istate;
    return vm;
}

void lu_vm_destroy(struct lu_vm* vm) {
    //
    free(vm);
}

static void lu_vm_push_new_record(struct lu_vm* vm,
                                  struct exectuable* executable) {
    struct activation_record record;
    record.executable = executable;
    record.ip = 0;
    record.max_register_count = executable->max_register_count;
    record.registers = nullptr;
    arrsetlen(record.registers, record.max_register_count);
    arrput(vm->records, record);
    vm->rp++;
}

struct lu_value lu_run_executable(struct lu_istate* state,
                                  struct exectuable* executable) {
    //
    //

    lu_vm_push_new_record(state->vm, executable);
    struct activation_record* record = &state->vm->records[state->vm->rp - 1];
    return lu_vm_run_record(state->vm, record);
}

struct lu_value lu_vm_run_record(struct lu_vm* vm,
                                 struct activation_record* record) {
    size_t instruction_count = record->executable->instructions_size;
record_start:
    while (record->ip < instruction_count) {
        const struct instruction* instr =
            &record->executable->instructions[record->ip++];
        switch (instr->opcode) {
            case OPCODE_LOAD_CONST: {
                record->registers[instr->register_index] =
                    record->executable->constants[instr->const_index];
                goto record_start;
            }
            case OPCODE_ADD: {
                record->registers[instr->dst] =
                    lu_vm_op_add(vm->istate, record->registers[instr->r1],
                                 record->registers[instr->r2]);
                goto record_start;
            }
            case OPCODE_SUB: {
                record->registers[instr->dst] =
                    lu_vm_op_sub(vm->istate, record->registers[instr->r1],
                                 record->registers[instr->r2]);
                goto record_start;
            }
            case OPCODE_MUL: {
                record->registers[instr->dst] =
                    lu_vm_op_mul(vm->istate, record->registers[instr->r1],
                                 record->registers[instr->r2]);
                goto record_start;
            }
            case OPCODE_TEST_LESS_THAN: {
                record->registers[instr->dst] =
                    lu_vm_op_lt(vm->istate, record->registers[instr->r1],
                                record->registers[instr->r2]);
                goto record_start;
            }
            case OPCODE_TEST_LESS_THAN_EQUAL: {
                record->registers[instr->dst] =
                    lu_vm_op_lte(vm->istate, record->registers[instr->r1],
                                 record->registers[instr->r2]);
                goto record_start;
            }
            case OPCODE_TEST_GREATER_THAN: {
                record->registers[instr->dst] =
                    lu_vm_op_gt(vm->istate, record->registers[instr->r1],
                                record->registers[instr->r2]);
                goto record_start;
            }
            case OPCODE_TEST_GREATER_THAN_EQUAL: {
                record->registers[instr->dst] =
                    lu_vm_op_gte(vm->istate, record->registers[instr->r1],
                                 record->registers[instr->r2]);
                goto record_start;
            }
            case OPCODE_TEST_EQUAL: {
                record->registers[instr->dst] =
                    lu_vm_op_eq(vm->istate, record->registers[instr->r1],
                                record->registers[instr->r2]);
                goto record_start;
            }
            case OPCODE_TEST_NOT_EQUAL: {
                record->registers[instr->dst] =
                    lu_vm_op_neq(vm->istate, record->registers[instr->r1],
                                 record->registers[instr->r2]);
                goto record_start;
            }
            case OPCODE_RET: {
                return record->registers[instr->register_index];
            }
            default: {
                break;
            }
        }
    }
    return lu_value_none();
}
