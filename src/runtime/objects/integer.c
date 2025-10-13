#include "runtime/objects/integer.h"

#include "runtime/heap.h"
#include "runtime/istate.h"
#include "runtime/object.h"

static lu_object_t* lu_int_add(lu_istate_t*, lu_object_t* a, lu_object_t* b) {}

lu_type_t* new_integer_type_object(heap_t* heap) {
    lu_type_t* type = heap_allocate_object(heap, sizeof(lu_type_t));

    type->binop_slots[0] = lu_int_add;

    return type;
}

lu_integer_t* lu_new_integer(lu_istate_t* state, int64_t value) {
    lu_integer_t* integer =
        heap_allocate_object(state->heap, sizeof(lu_integer_t));
    integer->value = value;
    integer->type = (lu_type_t*)state->type_registry[INTEGER_TYPE_SLOT];

    return integer;
}
