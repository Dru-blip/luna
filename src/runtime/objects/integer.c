#include "runtime/objects/integer.h"

#include "operator.h"
#include "runtime/heap.h"
#include "runtime/istate.h"
#include "runtime/object.h"

lu_type_t* Integer_type = nullptr;

LU_ARITH_OP(lu_int_add, Integer_type, lu_integer_t, lu_new_integer, +)
LU_ARITH_OP(lu_int_sub, Integer_type, lu_integer_t, lu_new_integer, -)
LU_ARITH_OP(lu_int_mul, Integer_type, lu_integer_t, lu_new_integer, *)

LU_COMPARE_OP(lu_int_lt, Integer_type, lu_integer_t, <)
LU_COMPARE_OP(lu_int_lte, Integer_type, lu_integer_t, <=)
LU_COMPARE_OP(lu_int_gt, Integer_type, lu_integer_t, >)
LU_COMPARE_OP(lu_int_gte, Integer_type, lu_integer_t, >=)
LU_COMPARE_OP(lu_int_eq, Integer_type, lu_integer_t, ==)
LU_COMPARE_OP(lu_int_neq, Integer_type, lu_integer_t, !=)

static lu_object_t* lu_int_div(lu_istate_t* state, lu_object_t* a,
                               lu_object_t* b) {
    if (a->type == Integer_type && a->type == b->type) {
        lu_integer_t* ib = (lu_integer_t*)b;
        if (ib->value == 0) {
            state->op_result = op_result_raised_error;
            state->error_message = "division by zero";
            return nullptr;
        }
        lu_integer_t* ia = (lu_integer_t*)a;
        return (lu_object_t*)lu_new_integer(state, ia->value / ib->value);
    }
    return nullptr;
}

lu_type_t* lu_integer_type_object_new(lu_istate_t* state) {
    lu_type_t* type = heap_allocate_object(state->heap, sizeof(lu_type_t));
    type->name = "int";

    type->binop_slots[binary_op_add] = lu_int_add;
    type->binop_slots[binary_op_sub] = lu_int_sub;
    type->binop_slots[binary_op_mul] = lu_int_mul;
    type->binop_slots[binary_op_div] = lu_int_div;

    type->binop_slots[binary_op_lt] = lu_int_lt;
    type->binop_slots[binary_op_lte] = lu_int_lte;
    type->binop_slots[binary_op_gt] = lu_int_gt;
    type->binop_slots[binary_op_gte] = lu_int_gte;
    type->binop_slots[binary_op_eq] = lu_int_eq;
    type->binop_slots[binary_op_neq] = lu_int_neq;

    type->finialize = object_default_finalize;

    Integer_type = type;

    return type;
}

lu_integer_t* lu_new_integer(lu_istate_t* state, int64_t value) {
    lu_integer_t* integer =
        heap_allocate_object(state->heap, sizeof(lu_integer_t));
    integer->value = value;
    integer->type = state->type_registry[INTEGER_TYPE_SLOT];

    return integer;
}
