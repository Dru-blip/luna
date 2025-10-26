#include "bytecode/vm.h"

#include <stdio.h>
#include <stdlib.h>

#include "bytecode/interpreter.h"
#include "bytecode/ir.h"
#include "stb_ds.h"
// #include "value.h"
#include "bytecode/vm_ops.h"
#include "value.h"

#define lu_has_error(vm) (vm)->istate->error != nullptr
#define IS_NUMERIC(a) (lu_is_int(a) || lu_is_bool(a))

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
            case OPCODE_LOAD_NONE: {
                record->registers[instr->register_index] = lu_value_none();
                goto record_start;
            }
            case OPCODE_LOAD_TRUE: {
                record->registers[instr->register_index] = lu_value_bool(true);
                goto record_start;
            }
            case OPCODE_LOAD_FALSE: {
                record->registers[instr->register_index] = lu_value_bool(false);
                goto record_start;
            }
            case OPCODE_MOV: {
                record->registers[instr->m_dst] =
                    record->registers[instr->m_src];
                goto record_start;
            }
            case OPCODE_UNARY_PLUS: {
                // Currently this is a no-op, but operation can be performed
                // based on type.
                goto record_start;
            }
            case OPCODE_UNARY_MINUS: {
                struct lu_value argument =
                    record->registers[instr->register_index];
                if (IS_NUMERIC(argument)) {
                    record->registers[instr->register_index] =
                        lu_value_int(-lu_as_int(argument));
                    goto record_start;
                }

                struct span span = lu_vm_current_ip_span(vm);
                const char* argument_type_name =
                    lu_value_get_type_name(argument);
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                         "invalid operand type for unary (-) : '%s'",
                         argument_type_name);
                lu_raise_error(vm->istate, lu_string_new(vm->istate, buffer),
                               &span);

                goto error_reporter;
            }
            case OPCODE_UNARY_NOT: {
                record->registers[instr->register_index] = lu_value_bool(
                    lu_is_falsy(record->registers[instr->register_index]));
                goto record_start;
            }

#define HANDLE_BINARY_INSTRUCTION(opcode, func)                                \
    case opcode: {                                                             \
        record->registers[instr->dst] = func(vm, record->registers[instr->r1], \
                                             record->registers[instr->r2]);    \
        if (lu_has_error(vm)) {                                                \
            goto error_reporter;                                               \
        }                                                                      \
        goto record_start;                                                     \
    }
                HANDLE_BINARY_INSTRUCTION(OPCODE_ADD, lu_vm_op_add);
                HANDLE_BINARY_INSTRUCTION(OPCODE_SUB, lu_vm_op_sub);
                HANDLE_BINARY_INSTRUCTION(OPCODE_MUL, lu_vm_op_mul);
                HANDLE_BINARY_INSTRUCTION(OPCODE_DIV, lu_vm_op_div);

                HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_LESS_THAN, lu_vm_op_lt);
                HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_LESS_THAN_EQUAL,
                                          lu_vm_op_lte);
                HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_GREATER_THAN,
                                          lu_vm_op_gt);
                HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_GREATER_THAN_EQUAL,
                                          lu_vm_op_gte);
                HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_EQUAL, lu_vm_op_eq);
                HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_NOT_EQUAL, lu_vm_op_neq);

            case OPCODE_RET: {
                return record->registers[instr->register_index];
            }
            case OPCODE_JUMP: {
                record->ip = instr->target_block_id;
                goto record_start;
            }
            case OPCODE_JMP_IF: {
                if (lu_is_truthy(record->registers[instr->cond])) {
                    record->ip = instr->true_block_id;
                } else {
                    record->ip = instr->false_block_id;
                }
                goto record_start;
            }
            default: {
                break;
            }
        }
    }
error_reporter:
    if (vm->istate->error) {
        struct lu_string* str = lu_as_string(lu_obj_get(
            vm->istate->error, lu_intern_string(vm->istate, "message")));
        struct lu_string* traceback = lu_as_string(lu_obj_get(
            vm->istate->error, lu_intern_string(vm->istate, "traceback")));
        printf("Error: %s\n", lu_string_get_cstring(str));
        if (traceback) {
            printf("%s\n", traceback->block->data);
        }
    }
    return lu_value_none();
}
