#pragma once

typedef enum binary_op {
    binary_op_add,
    binary_op_sub,
    binary_op_mul,
    binary_op_div,
    binary_op_mod,

    binary_op_gt,
    binary_op_gte,
    binary_op_lt,
    binary_op_lte,

    binary_op_eq,
    binary_op_neq,

    binary_op_shl,
    binary_op_shr,

    binary_op_land,
    binary_op_lor,
} binary_op_t;

typedef enum unary_op {
    unary_op_plus,
    unary_op_minus,
    unary_op_lnot,
} unary_op_t;

typedef enum assign_op {
    assign_op_simple,
    assign_op_add,
    assign_op_sub,
    assign_op_mul,
    assign_op_div,
    assign_op_mod,
} assign_op_t;

typedef enum postfix_op {
    postfix_call,
    postfix_member,
} postfix_op_t;

static const char* binary_op_labels[] = {
    "+",  "-",  "*", "/",  "%",

    ">",  ">=", "<", "<=",

    "==", "!=",

    "<<", ">>",

    "&&", "||",
};
