#include "runtime/objects/integer.h"

#include "operator.h"
#include "runtime/heap.h"
#include "runtime/istate.h"
#include "runtime/object.h"
#include "runtime/objects/boolean.h"

LU_ARITH_OP(lu_int_add, INTEGER_TYPE_SLOT, lu_integer_t, lu_new_integer, +)
LU_ARITH_OP(lu_int_sub, INTEGER_TYPE_SLOT, lu_integer_t, lu_new_integer, -)
LU_ARITH_OP(lu_int_mul, INTEGER_TYPE_SLOT, lu_integer_t, lu_new_integer, *)

LU_COMPARE_OP(lu_int_lt, INTEGER_TYPE_SLOT, lu_bool_t, <)
LU_COMPARE_OP(lu_int_lte, INTEGER_TYPE_SLOT, lu_bool_t, <=)
LU_COMPARE_OP(lu_int_gt, INTEGER_TYPE_SLOT, lu_bool_t, >)
LU_COMPARE_OP(lu_int_gte, INTEGER_TYPE_SLOT, lu_bool_t, >=)
LU_COMPARE_OP(lu_int_eq, INTEGER_TYPE_SLOT, lu_bool_t, ==)
LU_COMPARE_OP(lu_int_neq, INTEGER_TYPE_SLOT, lu_bool_t, !=)

lu_type_t* lu_integer_type_object_new(heap_t* heap) {
    lu_type_t* type = heap_allocate_object(heap, sizeof(lu_type_t));

    type->binop_slots[binary_op_add] = lu_int_add;
    type->binop_slots[binary_op_sub] = lu_int_sub;
    type->binop_slots[binary_op_mul] = lu_int_mul;

    type->binop_slots[binary_op_lt] = lu_int_lt;
    type->binop_slots[binary_op_lte] = lu_int_lte;
    type->binop_slots[binary_op_gt] = lu_int_gt;
    type->binop_slots[binary_op_gte] = lu_int_gte;
    type->binop_slots[binary_op_eq] = lu_int_eq;
    type->binop_slots[binary_op_neq] = lu_int_neq;

    type->finialize = object_default_finalize;

    return type;
}

lu_integer_t* lu_new_integer(lu_istate_t* state, int64_t value) {
    lu_integer_t* integer =
        heap_allocate_object(state->heap, sizeof(lu_integer_t));
    integer->value = value;
    integer->type = state->type_registry[INTEGER_TYPE_SLOT];

    return integer;
}
