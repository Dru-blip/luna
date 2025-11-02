#include <stdio.h>

#include "value.h"
#include "vm.h"

#define IS_NUMERIC(a) (lu_is_int(a) || lu_is_bool(a))

#define RAISE_TYPE_ERROR(vm, op, a, b)                                       \
    const char* lhs_type_name = lu_value_get_type_name(a);                   \
    const char* rhs_type_name = lu_value_get_type_name(b);                   \
    char buffer[256];                                                        \
    snprintf(buffer, sizeof(buffer),                                         \
             "invalid operand types for operation (%s) : '%s' and '%s'", op, \
             lhs_type_name, rhs_type_name);                                  \
    lu_raise_error(vm->istate, buffer);                                      \
    return lu_value_none();

static inline struct lu_value lu_vm_op_add(struct lu_vm* vm, struct lu_value a,
                                           struct lu_value b) {
    if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
        return lu_value_int(a.integer + b.integer);
    }

    if (lu_is_string(a) || lu_is_string(b)) {
        return lu_value_object(
            lu_cast(struct lu_object, lu_string_concat(vm->istate, a, b)));
    }

    RAISE_TYPE_ERROR(vm, "+", a, b)
}

static inline struct lu_value lu_vm_op_sub(struct lu_vm* vm, struct lu_value a,
                                           struct lu_value b) {
    if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
        return lu_value_int(a.integer - b.integer);
    }

    RAISE_TYPE_ERROR(vm, "-", a, b);
}

static inline struct lu_value lu_vm_op_mul(struct lu_vm* vm, struct lu_value a,
                                           struct lu_value b) {
    if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
        return lu_value_int(a.integer * b.integer);
    }

    RAISE_TYPE_ERROR(vm, "*", a, b);
}

static inline struct lu_value lu_vm_op_div(struct lu_vm* vm, struct lu_value a,
                                           struct lu_value b) {
    if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
        if (b.integer == 0) {
            lu_raise_error(vm->istate, "Division by zero");
            return lu_value_none();
        }
        return lu_value_int(a.integer * b.integer);
    }

    RAISE_TYPE_ERROR(vm, "/", a, b);
}

#define STR_CMP(a, b) (lu_strcmp(lu_as_string(a), lu_as_string(b)))

static inline struct lu_value lu_vm_op_lt(struct lu_vm* vm, struct lu_value a,
                                          struct lu_value b) {
    if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
        return lu_value_bool(a.integer < b.integer);
    }

    if (lu_is_string(a) && lu_is_string(b)) {
        return lu_value_bool(STR_CMP(a, b) < 0);
    }

    RAISE_TYPE_ERROR(vm, "<", a, b);
}

static inline struct lu_value lu_vm_op_lte(struct lu_vm* vm, struct lu_value a,
                                           struct lu_value b) {
    if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
        return lu_value_bool(a.integer <= b.integer);
    }

    if (lu_is_string(a) && lu_is_string(b)) {
        return lu_value_bool(STR_CMP(a, b) <= 0);
    }

    RAISE_TYPE_ERROR(vm, "<=", a, b);
}

static inline struct lu_value lu_vm_op_gt(struct lu_vm* vm, struct lu_value a,
                                          struct lu_value b) {
    if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
        return lu_value_bool(a.integer > b.integer);
    }

    if (lu_is_string(a) && lu_is_string(b)) {
        return lu_value_bool(STR_CMP(a, b) > 0);
    }

    RAISE_TYPE_ERROR(vm, ">", a, b);
}

static inline struct lu_value lu_vm_op_gte(struct lu_vm* vm, struct lu_value a,
                                           struct lu_value b) {
    if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
        return lu_value_bool(a.integer >= b.integer);
    }

    if (lu_is_string(a) && lu_is_string(b)) {
        return lu_value_bool(STR_CMP(a, b) >= 0);
    }

    RAISE_TYPE_ERROR(vm, ">=", a, b);
}

static inline struct lu_value lu_vm_op_eq(struct lu_vm* vm, struct lu_value a,
                                          struct lu_value b) {
    return lu_value_bool(lu_value_strict_equals(a, b));
}

static inline struct lu_value lu_vm_op_neq(struct lu_vm* vm, struct lu_value a,
                                           struct lu_value b) {
    return lu_value_bool(!lu_value_strict_equals(a, b));
}
