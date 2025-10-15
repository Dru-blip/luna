#include "runtime/objects/boolean.h"

#include "operator.h"
#include "runtime/istate.h"
#include "runtime/objects/integer.h"
#include "strings/interner.h"

lu_type_t* Bool_type = nullptr;

LU_ARITH_OP(lu_bool_add, Bool_type, lu_bool_t, lu_new_integer, +)
LU_ARITH_OP(lu_bool_sub, Bool_type, lu_bool_t, lu_new_integer, -)
LU_ARITH_OP(lu_bool_mul, Bool_type, lu_integer_t, lu_new_integer, *)

LU_COMPARE_OP(lu_bool_lt, Bool_type, lu_bool_t, <)
LU_COMPARE_OP(lu_bool_lte, Bool_type, lu_bool_t, <=)
LU_COMPARE_OP(lu_bool_gt, Bool_type, lu_bool_t, >)
LU_COMPARE_OP(lu_bool_gte, Bool_type, lu_bool_t, >=)
LU_COMPARE_OP(lu_bool_eq, Bool_type, lu_bool_t, ==)
LU_COMPARE_OP(lu_bool_neq, Bool_type, lu_bool_t, !=)

static lu_object_t* lu_bool_unplus(lu_istate_t* state, lu_object_t* a) {
    return (lu_object_t*)lu_new_integer(state, ((lu_bool_t*)a)->value ? 1 : 0);
}

static lu_object_t* lu_bool_unminus(lu_istate_t* state, lu_object_t* a) {
    return (lu_object_t*)lu_new_integer(
        state, ((lu_bool_t*)a)->value ? -((lu_integer_t*)a)->value : 0);
}

static lu_object_t* lu_bool_unnot(lu_istate_t* state, lu_object_t* a) {
    return ((lu_bool_t*)a)->value ? state->false_obj : state->true_obj;
}

lu_type_t* lu_bool_type_object_new(lu_istate_t* state) {
    lu_type_t* type = heap_allocate_object(state->heap, sizeof(lu_type_t));
    type->name = "bool";

    type->name_strobj = lu_intern_string(state->string_pool, "bool", 4);

    type->unop_slots[unary_op_plus] = lu_bool_unplus;
    type->unop_slots[unary_op_minus] = lu_bool_unminus;
    type->unop_slots[unary_op_lnot] = lu_bool_unnot;

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

    Bool_type = type;
    return type;
}

lu_bool_t* lu_new_bool(lu_istate_t* state, bool value) {
    lu_bool_t* bool_obj = heap_allocate_object(state->heap, sizeof(lu_bool_t));
    bool_obj->value = value;
    bool_obj->type = Bool_type;

    return bool_obj;
}
