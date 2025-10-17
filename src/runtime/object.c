#include "runtime/object.h"

#include "runtime/heap.h"
#include "runtime/istate.h"

lu_type_t* Base_type = nullptr;

void object_default_visit(lu_object_t* self,
                          struct lu_gc_objectset* live_cells) {
    //
    lu_gc_objectset_insert(live_cells, self->type);
}

void base_object_type_init(struct lu_istate* state) {
    Base_type = heap_allocate_object(state->heap, sizeof(lu_type_t));
    Base_type->visit = object_default_visit;
    Base_type->finalize = object_default_finalize;
    Base_type->type = Base_type;
}
