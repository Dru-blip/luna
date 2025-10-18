#pragma once

enum binary_op {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,

    OP_GT,
    OP_GTE,
    OP_LT,
    OP_LTE,

    OP_EQ,
    OP_NEQ,

    OP_SHL,
    OP_SHR,

    OP_LAND,
    OP_LOR,
};

enum unary_op {
    OP_PLUS,
    OP_MINUS,
    OP_LNOT,
};

enum assign_op {
    OP_ASSIGN_SIMPLE,
    OP_ASSIGN_ADD,
    OP_ASSIGN_SUB,
    OP_ASSIGN_MUL,
    OP_ASSIGN_DIV,
    OP_ASSIGN_MOD,
};

enum postfix_op {
    OP_CALL,
    OP_MEMBER,
};

static const char* binary_op_labels[] = {
    "+",  "-",  "*", "/",  "%",

    ">",  ">=", "<", "<=",

    "==", "!=",

    "<<", ">>",

    "&&", "||",
};
