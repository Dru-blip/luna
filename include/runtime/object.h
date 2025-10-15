#pragma once

#include <stddef.h>

typedef enum { obj_state_dead, obj_state_alive } obj_state_kind;

#define LUNA_OBJECT_HEADER  \
    struct lu_object* next; \
    obj_state_kind state;   \
    bool is_marked;         \
    struct lu_type* type;

typedef struct lu_object {
    LUNA_OBJECT_HEADER
} lu_object_t;

struct lu_istate;

typedef lu_object_t* (*binary_func)(struct lu_istate*, lu_object_t*,
                                    lu_object_t*);
typedef lu_object_t* (*unary_func)(struct lu_istate*, lu_object_t*);
typedef void (*finalize_func)(lu_object_t*);
typedef lu_object_t* (*to_string)(lu_object_t*);

typedef struct lu_type {
    LUNA_OBJECT_HEADER;

    size_t obj_size;
    char* name;
    lu_object_t* name_strobj;
    struct lu_type* base;

    finalize_func finialize;
    to_string tostr;
    unary_func unop_slots[3];
    binary_func binop_slots[15];
} lu_type_t;

static inline void object_default_finalize(lu_object_t* self) {}

#define LU_ARITH_OP(OP_NAME, TYPE_OBJ, TYPE_T, NEW_FUNC, OP)        \
    static lu_object_t* OP_NAME(lu_istate_t* state, lu_object_t* a, \
                                lu_object_t* b) {                   \
        if (a->type == TYPE_OBJ && a->type == b->type) {            \
            return (lu_object_t*)NEW_FUNC(                          \
                state, ((TYPE_T*)a)->value OP((TYPE_T*)b)->value);  \
        }                                                           \
        state->op_result = op_result_not_implemented;               \
        return nullptr;                                             \
    }

#define LU_COMPARE_OP(OP_NAME, TYPE_OBJ, TYPE_T, OP)                \
    static lu_object_t* OP_NAME(lu_istate_t* state, lu_object_t* a, \
                                lu_object_t* b) {                   \
        if (a->type == TYPE_OBJ && a->type == b->type) {            \
            return ((TYPE_T*)a)->value OP((TYPE_T*)b)->value        \
                       ? state->true_obj                            \
                       : state->false_obj;                          \
        }                                                           \
        state->op_result = op_result_not_implemented;               \
        return nullptr;                                             \
    }
