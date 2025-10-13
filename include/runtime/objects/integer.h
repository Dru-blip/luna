#pragma once

#include <stdint.h>

#include "runtime/heap.h"
#include "runtime/istate.h"
#include "runtime/object.h"

#define INTEGER_TYPE_SLOT 0

typedef struct lu_integer {
    LUNA_OBJECT_HEADER;
    int64_t value;
} lu_integer_t;

lu_type_t* new_integer_type_object(heap_t* heap);
lu_integer_t* lu_new_integer(lu_istate_t* state, int64_t value);
