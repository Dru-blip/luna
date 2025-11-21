#include "bytecode/vm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bytecode/interpreter.h"
#include "bytecode/ir.h"
#include "bytecode/register_list.h"
#include "luna.h"
#include "stb_ds.h"
// #include "value.h"
#include "bytecode/vm_ops.h"
#include "strbuf.h"
#include "string_interner.h"
#include "value.h"

#define REGISTER_POOL_SLOTS (1024 * 256)
#define lu_has_error(vm) (vm)->istate->error != nullptr
#define IS_NUMERIC(a) (lu_is_int(a) || lu_is_bool(a))

struct lu_vm* lu_vm_new(struct lu_istate* istate) {
    struct lu_vm* vm = malloc(sizeof(struct lu_vm));
    vm->rp = 0;
    vm->istate = istate;
    vm->global_object = lu_object_new(istate);
    register_list_init(&vm->reg_list, REGISTER_POOL_SLOTS);
    return vm;
}

void lu_vm_destroy(struct lu_vm* vm) {
    free(vm);
}

ALWAYS_INLINE static void lu_vm_push_new_record_with_globals(struct lu_vm* vm,
                                                             struct executable* executable,
                                                             struct lu_globals* globals) {
    struct activation_record record;
    record.executable = executable;
    record.ip = 0;

    record.globals = globals;

    record.max_register_count = executable->max_register_count;
    record.registers = register_list_alloc_registers(&vm->reg_list, executable->max_register_count);

    // arrsetlen(record.registers, record.max_register_count);

    // arrput(vm->records, record);
    vm->records[vm->rp++] = record;
}

static void lu_vm_push_new_record(struct lu_vm* vm, struct executable* executable) {
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
    // arrput(vm->records, record);
    vm->records[vm->rp++] = record;
}

struct lu_value lu_run_executable(struct lu_istate* state, struct executable* executable) {
    lu_vm_push_new_record(state->vm, executable);
    struct activation_record* record = &state->vm->records[state->vm->rp - 1];
    return lu_vm_run_record(state->vm, record, false);
}

ALWAYS_INLINE static inline bool check_arity(struct lu_function* func,
                                             uint32_t argc,
                                             struct lu_vm* vm) {
    if (!func->is_variadic && argc != func->param_count) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "function '%s' expects %ld argument(s), got %d",
                 lu_string_get_cstring(func->name), func->param_count, argc);
        lu_raise_error(vm->istate, buffer);
        return false;
    }
    return true;
}

ALWAYS_INLINE static inline bool check_is_function(struct lu_vm* vm,
                                                   struct lu_value* registers,
                                                   struct lu_value callee_val) {
    if (!lu_is_function(callee_val)) {
        const char* calle_type_name = lu_value_get_type_name(callee_val);
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                 "attempt to call a non function value"
                 " (%s)",
                 calle_type_name);
        lu_raise_error(vm->istate, buffer);
        return true;
    }
    return false;
}

struct lu_value lu_vm_run_record(struct lu_vm* vm,
                                 struct activation_record* record,
                                 bool as_callback) {
    register struct lu_value* registers = record->registers;
    register struct lu_value* global_fast_slots = record->globals->fast_slots;
    struct lu_object* named_globals = record->globals->named_slots;
    register struct executable* executable = record->executable;
    register struct instruction* instructions = executable->instructions;
    struct lu_value self = record->registers[0];
    register struct instruction* instr;
    static void* dispatch_table[] = {
        [OPCODE_LOAD_CONST] = &&handle_OPCODE_LOAD_CONST,
        [OPCODE_LOAD_NONE] = &&handle_OPCODE_LOAD_NONE,
        [OPCODE_LOAD_TRUE] = &&handle_OPCODE_LOAD_TRUE,
        [OPCODE_LOAD_FALSE] = &&handle_OPCODE_LOAD_FALSE,
        [OPCODE_MOV] = &&handle_OPCODE_MOV,
        [OPCODE_LOAD_GLOBAL_BY_INDEX] = &&handle_OPCODE_LOAD_GLOBAL_BY_INDEX,
        [OPCODE_STORE_GLOBAL_BY_INDEX] = &&handle_OPCODE_STORE_GLOBAL_BY_INDEX,
        [OPCODE_LOAD_GLOBAL_BY_NAME] = &&handle_OPCODE_LOAD_GLOBAL_BY_NAME,
        [OPCODE_STORE_GLOBAL_BY_NAME] = &&handle_OPCODE_STORE_GLOBAL_BY_NAME,
        [OPCODE_UNARY_PLUS] = &&handle_OPCODE_UNARY_PLUS,
        [OPCODE_UNARY_MINUS] = &&handle_OPCODE_UNARY_MINUS,
        [OPCODE_UNARY_NOT] = &&handle_OPCODE_UNARY_NOT,
        [OPCODE_NEW_ARRAY] = &&handle_OPCODE_NEW_ARRAY,
        [OPCODE_ARRAY_APPEND] = &&handle_OPCODE_ARRAY_APPEND,
        [OPCODE_NEW_OBJECT] = &&handle_OPCODE_NEW_OBJECT,
        [OPCODE_OBJECT_SET_PROPERTY] = &&handle_OPCODE_OBJECT_SET_PROPERTY,
        [OPCODE_OBJECT_GET_PROPERTY] = &&handle_OPCODE_OBJECT_GET_PROPERTY,
        [OPCODE_LOAD_SUBSCR] = &&handle_OPCODE_LOAD_SUBSCR,
        [OPCODE_STORE_SUBSCR] = &&handle_OPCODE_STORE_SUBSCR,

        [OPCODE_ADD] = &&handle_OPCODE_ADD,
        [OPCODE_SUB] = &&handle_OPCODE_SUB,
        [OPCODE_MUL] = &&handle_OPCODE_MUL,
        [OPCODE_DIV] = &&handle_OPCODE_DIV,

        [OPCODE_TEST_LESS_THAN] = &&handle_OPCODE_TEST_LESS_THAN,
        [OPCODE_TEST_GREATER_THAN] = &&handle_OPCODE_TEST_GREATER_THAN,
        [OPCODE_TEST_EQUAL] = &&handle_OPCODE_TEST_EQUAL,
        [OPCODE_TEST_NOT_EQUAL] = &&handle_OPCODE_TEST_NOT_EQUAL,
        [OPCODE_TEST_LESS_THAN_EQUAL] = &&handle_OPCODE_TEST_LESS_THAN_EQUAL,
        [OPCODE_TEST_GREATER_THAN_EQUAL] = &&handle_OPCODE_TEST_GREATER_THAN_EQUAL,
        [OPCODE_JUMP] = &&handle_OPCODE_JUMP,
        [OPCODE_JMP_IF] = &&handle_OPCODE_JMP_IF,
        [OPCODE_GET_ITER] = &&handle_OPCODE_GET_ITER,
        [OPCODE_ITER_NEXT] = &&handle_OPCODE_ITER_NEXT,

        [OPCODE_MAKE_FUNCTION] = &&handle_OPCODE_MAKE_FUNCTION,
        [OPCODE_CALL] = &&handle_OPCODE_CALL,
        [OPCODE_RET] = &&handle_OPCODE_RET,
        [OPCODE_HLT] = &&handle_OPCODE_HLT,

    };
#define CASE(opcode) handle_##opcode

#define LOAD_RECORD(record)                  \
    registers = record->registers;           \
    executable = record->executable;         \
    instructions = executable->instructions; \
    self = record->registers[0];

#define DISPATCH_NEXT()                      \
    do {                                     \
        instr = &instructions[record->ip++]; \
        goto* dispatch_table[instr->opcode]; \
    } while (0)

    DISPATCH_NEXT();

    CASE(OPCODE_LOAD_CONST) : {
        registers[instr->load_const.destination_reg] =
            executable->constants[instr->load_const.constant_index];
        DISPATCH_NEXT();
    }
#define HANDLE_LOAD_LITERAL(opcode, instr, val)  \
    CASE(opcode) : {                             \
        registers[instr->destination_reg] = val; \
        DISPATCH_NEXT();                         \
    }
    HANDLE_LOAD_LITERAL(OPCODE_LOAD_NONE, instr, lu_value_none());
    HANDLE_LOAD_LITERAL(OPCODE_LOAD_TRUE, instr, lu_value_bool(true));
    HANDLE_LOAD_LITERAL(OPCODE_LOAD_FALSE, instr, lu_value_bool(false));

#define HANDLE_MOV(opcode, instr, dst, src)                 \
    CASE(opcode) : {                                        \
        dst[instr->mov.dest_reg] = src[instr->mov.src_reg]; \
        DISPATCH_NEXT();                                    \
    }

    HANDLE_MOV(OPCODE_MOV, instr, registers, registers)
    HANDLE_MOV(OPCODE_LOAD_GLOBAL_BY_INDEX, instr, registers, global_fast_slots)
    HANDLE_MOV(OPCODE_STORE_GLOBAL_BY_INDEX, instr, global_fast_slots, registers)

    CASE(OPCODE_STORE_GLOBAL_BY_NAME) : {
        struct lu_string* name = executable->identifier_table[instr->pair.snd];
        lu_obj_set(named_globals, name, registers[instr->pair.fst]);
        DISPATCH_NEXT();
    }
    CASE(OPCODE_LOAD_GLOBAL_BY_NAME) : {
        struct lu_string* name = executable->identifier_table[instr->pair.fst];
        struct lu_value value = lu_obj_get(named_globals, name);
        if (!lu_is_undefined(value)) {
            registers[instr->pair.snd] = value;
            DISPATCH_NEXT();
        }

        value = lu_obj_get(vm->global_object, name);
        if (!lu_is_undefined(value)) {
            registers[instr->pair.snd] = value;
            DISPATCH_NEXT();
        }

        lu_raise_error(vm->istate, "undeclared variable");
        goto error_reporter;
    }
    CASE(OPCODE_UNARY_PLUS) : {
        // Currently this is a no-op, but operation can be performed
        // based on type.
        DISPATCH_NEXT();
    }
    CASE(OPCODE_UNARY_MINUS) : {
        struct lu_value argument = registers[instr->destination_reg];
        if (IS_NUMERIC(argument)) {
            registers[instr->destination_reg] = lu_value_int(-lu_as_int(argument));
            DISPATCH_NEXT();
        }

        const char* argument_type_name = lu_value_get_type_name(argument);
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "invalid operand type for unary (-) : '%s'",
                 argument_type_name);
        lu_raise_error(vm->istate, buffer);

        goto error_reporter;
    }
    CASE(OPCODE_UNARY_NOT) : {
        registers[instr->destination_reg] =
            lu_value_bool(lu_is_falsy(registers[instr->destination_reg]));
        DISPATCH_NEXT();
    }
    CASE(OPCODE_NEW_ARRAY) : {
        registers[instr->destination_reg] = lu_value_object(lu_array_new(vm->istate));
        DISPATCH_NEXT();
    }
    CASE(OPCODE_ARRAY_APPEND) : {
        struct lu_array* array = lu_as_array(registers[instr->pair.fst]);
        lu_array_push(array, registers[instr->pair.snd]);
        DISPATCH_NEXT();
    }
    CASE(OPCODE_NEW_OBJECT) : {
        registers[instr->destination_reg] = lu_value_object(lu_object_new(vm->istate));
        DISPATCH_NEXT();
    }
    CASE(OPCODE_OBJECT_SET_PROPERTY) : {
        struct lu_value obj_val = registers[instr->binary_op.result_reg];
        if (!lu_is_object(obj_val)) {
            lu_raise_error(vm->istate, "invalid member access on non object value");
            goto error_reporter;
        }
        struct lu_string* key = executable->identifier_table[instr->binary_op.left_reg];
        struct lu_value value = registers[instr->binary_op.right_reg];
        lu_obj_set(lu_as_object(obj_val), key, value);
        DISPATCH_NEXT();
    }
    CASE(OPCODE_OBJECT_GET_PROPERTY) : {
        struct lu_value obj_val = registers[instr->binary_op.left_reg];
        struct lu_string* key = executable->identifier_table[instr->binary_op.right_reg];
        if (!lu_is_object(obj_val)) {
            lu_raise_error(vm->istate, "invalid member access on non object value");
            goto error_reporter;
        }
        struct lu_value value = lu_obj_get(lu_as_object(obj_val), key);
        if (lu_is_undefined(value)) {
            char buffer[256];
            struct strbuf sb;
            strbuf_init_static(&sb, buffer, sizeof(buffer));
            strbuf_appendf(&sb, "object has no property '%s'", lu_string_get_cstring(key));
            lu_raise_error(vm->istate, buffer);
            goto error_reporter;
        }
        if (lu_is_function(value)) {
            value = lu_value_object(
                lu_wrap_bound_function(vm->istate, lu_as_function(value), lu_as_object(obj_val)));
        }
        registers[instr->binary_op.result_reg] = value;

        DISPATCH_NEXT();
    }
    CASE(OPCODE_LOAD_SUBSCR) : {
        struct lu_value obj_val = registers[instr->binary_op.left_reg];
        struct lu_value computed_index = registers[instr->binary_op.right_reg];
        if (lu_is_object(obj_val)) {
            struct lu_value result =
                lu_as_object(obj_val)->vtable->subscr(vm, lu_as_object(obj_val), computed_index);
            registers[instr->binary_op.result_reg] = result;
            DISPATCH_NEXT();
        }

        lu_raise_error(vm->istate, "invalid member access on non object value");
        goto error_reporter;
    }
    CASE(OPCODE_STORE_SUBSCR) : {
        struct lu_value obj_val = registers[instr->binary_op.left_reg];
        struct lu_value computed_index = registers[instr->binary_op.right_reg];
        if (lu_is_array(obj_val)) {
            if (lu_is_int(computed_index)) {
                int64_t index = lu_as_int(computed_index);
                if (index < 0) {
                    goto invalid_array_index;
                }
                if (lu_array_set(lu_as_array(obj_val), index,
                                 registers[instr->binary_op.result_reg]) > 0) {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), "index %ld out of bounds (array length %ld)",
                             index, lu_array_length(lu_as_array(obj_val)));
                    lu_raise_error(vm->istate, buffer);
                    goto error_reporter;
                }
                DISPATCH_NEXT();
            }
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "array index must be an integer ,got %s",
                     lu_value_get_type_name(computed_index));
            lu_raise_error(vm->istate, buffer);
            goto error_reporter;
        }
        if (!lu_is_string(computed_index)) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "object accessor must be a string , got %s",
                     lu_value_get_type_name(computed_index));
            lu_raise_error(vm->istate, buffer);
            return lu_value_none();
        }

        lu_obj_set(lu_as_object(obj_val), lu_as_string(computed_index),
                   registers[instr->binary_op.result_reg]);
        DISPATCH_NEXT();
    }

#define HANDLE_BINARY_INSTRUCTION(opcode, func)                                                    \
    CASE(opcode) : {                                                                               \
        registers[instr->binary_op.result_reg] =                                                   \
            func(vm, registers[instr->binary_op.left_reg], registers[instr->binary_op.right_reg]); \
        if (lu_has_error(vm)) {                                                                    \
            goto error_reporter;                                                                   \
        }                                                                                          \
        DISPATCH_NEXT();                                                                           \
    }
    HANDLE_BINARY_INSTRUCTION(OPCODE_ADD, lu_vm_op_add);
    HANDLE_BINARY_INSTRUCTION(OPCODE_SUB, lu_vm_op_sub);
    HANDLE_BINARY_INSTRUCTION(OPCODE_MUL, lu_vm_op_mul);
    HANDLE_BINARY_INSTRUCTION(OPCODE_DIV, lu_vm_op_div);

    HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_LESS_THAN, lu_vm_op_lt);
    HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_LESS_THAN_EQUAL, lu_vm_op_lte);
    HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_GREATER_THAN, lu_vm_op_gt);
    HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_GREATER_THAN_EQUAL, lu_vm_op_gte);
    HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_EQUAL, lu_vm_op_eq);
    HANDLE_BINARY_INSTRUCTION(OPCODE_TEST_NOT_EQUAL, lu_vm_op_neq);

    CASE(OPCODE_JUMP) : {
        record->ip = instr->jmp.target_offset;
        DISPATCH_NEXT();
    }
    CASE(OPCODE_JMP_IF) : {
        if (lu_is_truthy(registers[instr->jmp_if.condition_reg])) {
            record->ip = instr->jmp_if.true_block_id;
        } else {
            record->ip = instr->jmp_if.false_block_id;
        }
        DISPATCH_NEXT();
    }
    CASE(OPCODE_GET_ITER) : {
        struct lu_value iterable = registers[instr->pair.fst];
        if (!lu_is_object(iterable)) {
            lu_raise_error(vm->istate, "cannot iterate over non-object");
            goto error_reporter;
        }

        struct lu_value iterator_val =
            lu_obj_get(lu_as_object(iterable), vm->istate->names.iterator);
        if (lu_is_undefined(iterator_val) || !lu_is_function(iterator_val)) {
            lu_raise_error(vm->istate, "object is not iterable");
            goto error_reporter;
        }

        registers[instr->pair.snd] =
            lu_call(vm, lu_as_object(iterable), lu_as_function(iterator_val), nullptr, 0, false);
        if (lu_has_error(vm)) {
            goto error_reporter;
        }

        DISPATCH_NEXT();
    }
    CASE(OPCODE_ITER_NEXT) : {
        struct lu_value iterator_val = registers[instr->iter_next.iterator_reg];
        struct lu_value next_func = lu_obj_get(lu_as_object(iterator_val), vm->istate->names.next);

        if (!lu_is_function(next_func)) {
            lu_raise_error(vm->istate, "next() is not a function");
            goto error_reporter;
        }
        struct lu_value next_val =
            lu_call(vm, lu_as_object(iterator_val), lu_as_function(next_func), nullptr, 0, false);

        if (!lu_is_object(next_val)) {
            lu_raise_error(vm->istate, "next() returned non object");
            goto error_reporter;
        }

        struct lu_value done_val = lu_obj_get(lu_as_object(next_val), vm->istate->names.done);
        if (lu_as_int(done_val)) {
            record->ip = instr->iter_next.jmp_offset;
            DISPATCH_NEXT();
        }

        registers[instr->iter_next.loop_var_reg] =
            lu_obj_get(lu_as_object(next_val), vm->istate->names.value);

        if (lu_has_error(vm)) {
            goto error_reporter;
        }

        DISPATCH_NEXT();
    }
    CASE(OPCODE_MAKE_FUNCTION) : {
        struct executable* fn_executable = executable->constants[instr->binary_op.left_reg].object;
        struct lu_string* func_name = executable->identifier_table[instr->binary_op.right_reg];
        struct lu_function* func =
            lu_function_new(vm->istate, func_name, vm->istate->running_module, fn_executable);
        registers[instr->binary_op.result_reg] = lu_value_object(func);
        DISPATCH_NEXT();
    }

#define GET_FUNCTION(callee_)                                                                   \
    lu_as_function(callee_)->type == FUNCTION_BOUND ? lu_as_function(callee_)->bound_func->func \
                                                    : lu_as_function(callee_);
    CASE(OPCODE_CALL) : {
        struct lu_value callee_val = registers[instr->call.callee_reg];

        if (check_is_function(vm, registers, callee_val))
            goto error_reporter;

        struct activation_record* parent_record = record;

        struct lu_function* func = GET_FUNCTION(callee_val);

        if (!check_arity(func, instr->call.argc, vm))
            goto error_reporter;

        // Refactor:
        // ---------------------------
        if (func->type == FUNCTION_NATIVE) {
            struct lu_value self = parent_record->registers[instr->call.self_reg];

            for (uint32_t i = 0; i < instr->call.argc; i++) {
                vm->native_args[i] = registers[instr->call.args_reg[i]];
            }
            registers[instr->call.ret_reg] =
                func->func(vm, lu_as_object(self), vm->native_args, instr->call.argc);
            if (lu_has_error(vm)) {
                goto error_reporter;
            }
            DISPATCH_NEXT();
        } else {
            if (vm->rp + 1 >= VM_MAX_RECORDS) {
                lu_raise_error(vm->istate, "Stack overflow: maximum call stack reached");
                goto error_reporter;
            }
            enum lu_function_type func_type = func->type;
            struct lu_bound_function* bound_func = func->bound_func;
            func = func_type == FUNCTION_BOUND ? bound_func->func : func;
            lu_vm_push_new_record_with_globals(vm, func->executable, record->globals);
            record = &vm->records[vm->rp - 1];
            LOAD_RECORD(record);
            record->caller_ret_reg = instr->call.ret_reg;

            registers[0] = func_type == FUNCTION_BOUND
                               ? lu_value_object(bound_func->self)
                               : parent_record->registers[instr->call.self_reg];
            for (uint32_t i = 0; i < instr->call.argc; i++) {
                registers[i + 1] = parent_record->registers[instr->call.args_reg[i]];
            }
            DISPATCH_NEXT();
        }
        // ---------------------------
    }
    CASE(OPCODE_RET) : {
        struct activation_record child_record = vm->records[--vm->rp];

        if (vm->rp < 1 || vm->istate->running_module != vm->istate->main_module || as_callback) {
            return child_record.registers[instr->destination_reg];
        }

        record = &vm->records[vm->rp - 1];
        LOAD_RECORD(record);
        registers[child_record.caller_ret_reg] = child_record.registers[instr->destination_reg];

        //-----------------
        // Should investigate
        //  Observation:
        //   why freeing registers makes it go very faster. (i forgot to free them before,causing
        //   memory leaks) its 3X difference in speed.
        // *** maybe its cache related thing?
        register_list_free_registers(&vm->reg_list, child_record.max_register_count);
        //-----------------

        DISPATCH_NEXT();
    }
    CASE(OPCODE_HLT) : return lu_value_none();

invalid_array_index:
    lu_raise_error(vm->istate, "invalid index");
#include "ansi_color_codes.h"
error_reporter:
    if (vm->istate->running_module != vm->istate->main_module) {
        return lu_value_none();
    }
    struct lu_string* str = lu_as_string(lu_obj_get(vm->istate->error, vm->istate->names.message));
    struct lu_string* traceback =
        lu_as_string(lu_obj_get(vm->istate->error, vm->istate->names.traceback));
    printf(BRED "Error" WHT ": %s\n" reset, lu_string_get_cstring(str));
    printf("%s\n", traceback->block->data);
    return lu_value_none();
}
