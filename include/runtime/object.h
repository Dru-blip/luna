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
typedef void (*finalize_func)(lu_object_t*);
typedef lu_object_t* (*to_string)(lu_object_t*);

typedef struct lu_type {
    LUNA_OBJECT_HEADER;

    size_t obj_size;
    lu_object_t* name_strobj;
    struct lu_type* base;

    finalize_func finialize;
    to_string tostr;
    binary_func binop_slots[15];
} lu_type_t;
