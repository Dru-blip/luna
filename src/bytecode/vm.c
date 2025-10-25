#include "bytecode/vm.h"

#include <stdlib.h>

#include "bytecode/interpreter.h"
#include "stb_ds.h"
// #include "value.h"

struct lu_vm* lu_vm_new() {
    struct lu_vm* vm = malloc(sizeof(struct lu_vm));
    vm->records = nullptr;
    vm->rp = 0;
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
    record.max_register_count = 1;
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
                record->registers[0] =
                    record->executable->constants[instr->const_index];
                goto record_start;
            }
            case OPCODE_RET: {
                return record->registers[0];
            }
            default: {
                break;
            }
        }
    }
    return lu_value_none();
}
