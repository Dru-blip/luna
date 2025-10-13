#include "runtime/objects/boolean.h"

#include "operator.h"
#include "runtime/objects/integer.h"

LU_ARITH_OP(lu_bool_add, BOOL_TYPE_SLOT, lu_bool_t, lu_new_integer, +)
LU_ARITH_OP(lu_bool_sub, BOOL_TYPE_SLOT, lu_bool_t, lu_new_integer, -)
LU_ARITH_OP(lu_bool_mul, BOOL_TYPE_SLOT, lu_integer_t, lu_new_integer, *)

LU_COMPARE_OP(lu_bool_lt, BOOL_TYPE_SLOT, lu_bool_t, <)
LU_COMPARE_OP(lu_bool_lte, BOOL_TYPE_SLOT, lu_bool_t, <=)
LU_COMPARE_OP(lu_bool_gt, BOOL_TYPE_SLOT, lu_bool_t, >)
LU_COMPARE_OP(lu_bool_gte, BOOL_TYPE_SLOT, lu_bool_t, >=)
LU_COMPARE_OP(lu_bool_eq, BOOL_TYPE_SLOT, lu_bool_t, ==)
LU_COMPARE_OP(lu_bool_neq, BOOL_TYPE_SLOT, lu_bool_t, !=)

lu_type_t* lu_bool_type_object_new(heap_t* heap) {
    lu_type_t* type = heap_allocate_object(heap, sizeof(lu_type_t));

    type->binop_slots[binary_op_add] = lu_bool_add;
    type->binop_slots[binary_op_sub] = lu_bool_sub;
    type->binop_slots[binary_op_mul] = lu_bool_mul;

    type->binop_slots[binary_op_lt] = lu_bool_lt;
    type->binop_slots[binary_op_lte] = lu_bool_lte;
    type->binop_slots[binary_op_gt] = lu_bool_gt;
    type->binop_slots[binary_op_gte] = lu_bool_gte;
    type->binop_slots[binary_op_eq] = lu_bool_eq;
    type->binop_slots[binary_op_neq] = lu_bool_neq;

    type->finialize = object_default_finalize;
    return type;
}

lu_bool_t* lu_new_bool(lu_istate_t* state, bool value) {
    lu_bool_t* bool_obj = heap_allocate_object(state->heap, sizeof(lu_bool_t));
    bool_obj->value = value;
    bool_obj->type = state->type_registry[BOOL_TYPE_SLOT];

    return bool_obj;
}
