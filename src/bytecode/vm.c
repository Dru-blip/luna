#include "bytecode/vm.h"

#include <stdint.h>
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

static void lu_vm_push_new_record_with_globals(struct lu_vm* vm,
                                               struct executable* executable,
                                               struct lu_globals* globals) {
    struct activation_record record;
    record.executable = executable;
    record.ip = 0;

    record.globals = globals;

    record.max_register_count = executable->max_register_count;
    record.registers = nullptr;
    arrsetlen(record.registers, record.max_register_count);

    arrput(vm->records, record);
    vm->rp++;
}

static void lu_vm_push_new_record(struct lu_vm* vm,
                                  struct executable* executable) {
    struct activation_record record;
    record.executable = executable;
    record.ip = 0;
    record.globals = malloc(sizeof(struct lu_globals));
    record.globals->fast_slots = nullptr;
    arrsetlen(record.globals->fast_slots, executable->global_variable_count);
    record.globals->named_slots = lu_object_new(vm->istate);
    record.max_register_count = executable->max_register_count;
    record.registers = nullptr;
    arrsetlen(record.registers, record.max_register_count);
    arrput(vm->records, record);
    vm->rp++;
}

struct lu_value lu_run_executable(struct lu_istate* state,
                                  struct executable* executable) {
    lu_vm_push_new_record(state->vm, executable);
    struct activation_record* record = &state->vm->records[state->vm->rp - 1];
    return lu_vm_run_record(state->vm, record);
}

struct lu_value lu_vm_run_record(struct lu_vm* vm,
                                 struct activation_record* record) {
record_start:
    size_t instruction_count = record->executable->instructions_size;
loop_start:
    while (record->ip < instruction_count) {
        const struct instruction* instr =
            &record->executable->instructions[record->ip++];
        switch (instr->opcode) {
            case OPCODE_LOAD_CONST: {
                record->registers[instr->load_const.destination_reg] =
                    record->executable
                        ->constants[instr->load_const.constant_index];
                goto loop_start;
            }
            case OPCODE_LOAD_NONE: {
                record->registers[instr->destination_reg] = lu_value_none();
                goto loop_start;
            }
            case OPCODE_LOAD_TRUE: {
                record->registers[instr->destination_reg] = lu_value_bool(true);
                goto loop_start;
            }
            case OPCODE_LOAD_FALSE: {
                record->registers[instr->destination_reg] =
                    lu_value_bool(false);
                goto loop_start;
            }
            case OPCODE_MOV: {
                record->registers[instr->mov.dest_reg] =
                    record->registers[instr->mov.src_reg];
                goto loop_start;
            }
            case OPCODE_LOAD_GLOBAL_BY_INDEX: {
                record->registers[instr->mov.dest_reg] =
                    record->globals->fast_slots[instr->mov.src_reg];
                goto loop_start;
            }
            case OPCODE_STORE_GLOBAL_BY_INDEX: {
                record->globals->fast_slots[instr->mov.dest_reg] =
                    record->registers[instr->mov.src_reg];
                goto loop_start;
            }
            case OPCODE_STORE_GLOBAL_BY_NAME: {
                struct lu_string* name =
                    record->executable->identifier_table[instr->pair.snd];
                lu_obj_set(record->globals->named_slots, name,
                           record->registers[instr->pair.fst]);
                goto loop_start;
            }
            case OPCODE_LOAD_GLOBAL_BY_NAME: {
                struct lu_string* name =
                    record->executable->identifier_table[instr->pair.fst];
                struct lu_value value =
                    lu_obj_get(record->globals->named_slots, name);
                if (!lu_is_undefined(value)) {
                    record->registers[instr->pair.snd] = value;
                }
                // TODO: raise error undeclared identifier
                goto loop_start;
            }
            case OPCODE_UNARY_PLUS: {
                // Currently this is a no-op, but operation can be performed
                // based on type.
                goto loop_start;
            }
            case OPCODE_UNARY_MINUS: {
                struct lu_value argument =
                    record->registers[instr->destination_reg];
                if (IS_NUMERIC(argument)) {
                    record->registers[instr->destination_reg] =
                        lu_value_int(-lu_as_int(argument));
                    goto loop_start;
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
                record->registers[instr->destination_reg] = lu_value_bool(
                    lu_is_falsy(record->registers[instr->destination_reg]));
                goto loop_start;
            }

#define HANDLE_BINARY_INSTRUCTION(opcode, func)                    \
    case opcode: {                                                 \
        record->registers[instr->binary_op.result_reg] =           \
            func(vm, record->registers[instr->binary_op.left_reg], \
                 record->registers[instr->binary_op.right_reg]);   \
        if (lu_has_error(vm)) {                                    \
            goto error_reporter;                                   \
        }                                                          \
        goto loop_start;                                           \
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

            case OPCODE_JUMP: {
                record->ip = instr->jmp.target_offset;
                goto loop_start;
            }
            case OPCODE_JMP_IF: {
                if (lu_is_truthy(
                        record->registers[instr->jmp_if.condition_reg])) {
                    record->ip = instr->jmp_if.true_block_id;
                } else {
                    record->ip = instr->jmp_if.false_block_id;
                }
                goto loop_start;
            }
            case OPCODE_MAKE_FUNCTION: {
                struct executable* executable =
                    record->executable->constants[instr->binary_op.left_reg]
                        .object;
                struct lu_string* func_name =
                    record->executable
                        ->identifier_table[instr->binary_op.right_reg];
                struct lu_function* func =
                    lu_function_new(vm->istate, func_name,
                                    vm->istate->running_module, executable);
                record->registers[instr->binary_op.result_reg] =
                    lu_value_object(func);
                goto loop_start;
            }
            case OPCODE_CALL: {
                struct lu_value callee_val =
                    record->registers[instr->call.callee_reg];
                struct lu_function* func = lu_as_function(callee_val);
                struct activation_record* parent_record = record;
                lu_vm_push_new_record_with_globals(vm, func->executable,
                                                   record->globals);
                record = &vm->records[vm->rp - 1];
                record->caller_ret_reg = instr->call.ret_reg;
                for (uint32_t i = 0; i < instr->call.argc; i++) {
                    record->registers[i] =
                        parent_record->registers[instr->call.args_reg[i]];
                }
                goto record_start;
            }
            case OPCODE_RET: {
                const struct activation_record child_record =
                    arrpop(vm->records);
                vm->rp--;

                if (vm->rp == 1) {
                    return child_record.registers[instr->destination_reg];
                }

                record = &vm->records[vm->rp - 1];
                record->registers[record->caller_ret_reg] =
                    child_record.registers[instr->destination_reg];
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
