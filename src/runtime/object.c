#include "runtime/object.h"

#include "runtime/heap.h"

void object_default_visit(lu_object_t* self,
                          struct lu_gc_objectset* live_cells) {
    //
    lu_gc_objectset_insert(live_cells, self->type);
}
