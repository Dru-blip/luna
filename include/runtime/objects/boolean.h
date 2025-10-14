
#pragma once

#include <stdint.h>

#include "runtime/heap.h"
#include "runtime/istate.h"
#include "runtime/object.h"

#define BOOL_TYPE_SLOT 1

typedef struct lu_bool {
    LUNA_OBJECT_HEADER;
    bool value;
} lu_bool_t;

extern lu_type_t* Bool_type;

lu_type_t* lu_bool_type_object_new(lu_istate_t* state);
lu_bool_t* lu_new_bool(lu_istate_t* state, bool value);
