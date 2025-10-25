#include "value.h"

static inline struct lu_value lu_vm_op_add(struct lu_istate* state,
                                           struct lu_value a,
                                           struct lu_value b) {
    //
    if ((lu_is_int(a) && lu_is_int(b)) || (lu_is_bool(a) && lu_is_bool(b))) {
        return lu_value_int(a.integer + b.integer);
    }

    if ((lu_is_int(a) || lu_is_int(b)) && (lu_is_bool(a) || lu_is_bool(b))) {
        return lu_value_int(a.integer + b.integer);
    }

    if (lu_is_string(a) || lu_is_string(b)) {
        return lu_value_object(
            lu_cast(struct lu_object, lu_string_concat(state, a, b)));
    }

    // TODO: raise error

    return lu_value_none();
}

static inline struct lu_value lu_vm_op_sub(struct lu_istate* state,
                                           struct lu_value a,
                                           struct lu_value b) {
    //
    if ((lu_is_int(a) && lu_is_int(b)) || (lu_is_bool(a) && lu_is_bool(b))) {
        return lu_value_int(a.integer - b.integer);
    }

    if ((lu_is_int(a) || lu_is_int(b)) && (lu_is_bool(a) || lu_is_bool(b))) {
        return lu_value_int(a.integer - b.integer);
    }

    // TODO: raise error

    return lu_value_none();
}

static inline struct lu_value lu_vm_op_mul(struct lu_istate* state,
                                           struct lu_value a,
                                           struct lu_value b) {
    //
    if ((lu_is_int(a) && lu_is_int(b)) || (lu_is_bool(a) && lu_is_bool(b))) {
        return lu_value_int(a.integer * b.integer);
    }

    if ((lu_is_int(a) || lu_is_int(b)) && (lu_is_bool(a) || lu_is_bool(b))) {
        return lu_value_int(a.integer * b.integer);
    }

    // TODO: raise error

    return lu_value_none();
}

static inline struct lu_value lu_vm_op_lt(struct lu_istate* state,
                                          struct lu_value a,
                                          struct lu_value b) {
    //
    if ((lu_is_int(a) && lu_is_int(b)) || (lu_is_bool(a) && lu_is_bool(b))) {
        return lu_value_bool(a.integer < b.integer);
    }

    if ((lu_is_int(a) || lu_is_int(b)) && (lu_is_bool(a) || lu_is_bool(b))) {
        return lu_value_bool(a.integer < b.integer);
    }

    // TODO: raise error
    return lu_value_bool(false);
}

static inline struct lu_value lu_vm_op_lte(struct lu_istate* state,
                                           struct lu_value a,
                                           struct lu_value b) {
    //
    if ((lu_is_int(a) && lu_is_int(b)) || (lu_is_bool(a) && lu_is_bool(b))) {
        return lu_value_bool(a.integer <= b.integer);
    }

    if ((lu_is_int(a) || lu_is_int(b)) && (lu_is_bool(a) || lu_is_bool(b))) {
        return lu_value_bool(a.integer <= b.integer);
    }

    // TODO: raise error
    return lu_value_bool(false);
}

static inline struct lu_value lu_vm_op_gt(struct lu_istate* state,
                                          struct lu_value a,
                                          struct lu_value b) {
    //
    if ((lu_is_int(a) && lu_is_int(b)) || (lu_is_bool(a) && lu_is_bool(b))) {
        return lu_value_bool(a.integer > b.integer);
    }

    if ((lu_is_int(a) || lu_is_int(b)) && (lu_is_bool(a) || lu_is_bool(b))) {
        return lu_value_bool(a.integer > b.integer);
    }

    // TODO: raise error
    return lu_value_bool(false);
}

static inline struct lu_value lu_vm_op_gte(struct lu_istate* state,
                                           struct lu_value a,
                                           struct lu_value b) {
    //
    if ((lu_is_int(a) && lu_is_int(b)) || (lu_is_bool(a) && lu_is_bool(b))) {
        return lu_value_bool(a.integer >= b.integer);
    }

    if ((lu_is_int(a) || lu_is_int(b)) && (lu_is_bool(a) || lu_is_bool(b))) {
        return lu_value_bool(a.integer >= b.integer);
    }

    // TODO: raise error
    return lu_value_bool(false);
}

static inline struct lu_value lu_vm_op_eq(struct lu_istate* state,
                                          struct lu_value a,
                                          struct lu_value b) {
    //
    if ((lu_is_int(a) && lu_is_int(b)) || (lu_is_bool(a) && lu_is_bool(b))) {
        return lu_value_bool(a.integer == b.integer);
    }

    if ((lu_is_int(a) || lu_is_int(b)) && (lu_is_bool(a) || lu_is_bool(b))) {
        return lu_value_bool(a.integer == b.integer);
    }

    // TODO: raise error
    return lu_value_bool(false);
}

static inline struct lu_value lu_vm_op_neq(struct lu_istate* state,
                                           struct lu_value a,
                                           struct lu_value b) {
    //
    if ((lu_is_int(a) && lu_is_int(b)) || (lu_is_bool(a) && lu_is_bool(b))) {
        return lu_value_bool(a.integer != b.integer);
    }

    if ((lu_is_int(a) || lu_is_int(b)) && (lu_is_bool(a) || lu_is_bool(b))) {
        return lu_value_bool(a.integer != b.integer);
    }

    // TODO: raise error
    return lu_value_bool(false);
}
