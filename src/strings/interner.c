#include "strings/interner.h"

#include <stdlib.h>

#include "arena.h"
#include "runtime/heap.h"
#include "runtime/objects/strobj.h"
#include "strings/fly_string.h"

string_interner_t* lu_string_interner_init(heap_t* heap) {
    string_interner_t* interner = malloc(sizeof(string_interner_t));
    interner->root = nullptr;
    interner->nstrings = 0;
    interner->heap = heap;
    arena_init(&interner->node_arena);
    arena_init(&interner->string_arena);

    return interner;
}

lu_string_t* lu_intern_string(string_interner_t* interner, char* str,
                              size_t str_len) {
    return fly_string_insert(interner, str, str_len);
}

lu_string_t* lu_intern_string_lookup(string_interner_t* interner, char* str,
                                     size_t str_len) {
    //
    return fly_string_lookup(interner->root, str, str_len);
}
