#pragma once

#include "arena.h"
#include "runtime/heap.h"
#include "runtime/objects/strobj.h"
#include "strings/fly_string.h"

typedef struct string_interner {
    size_t nstrings;
    fly_string_node_t* root;
    arena_t string_arena;
    arena_t node_arena;
    heap_t* heap;
} string_interner_t;

string_interner_t* lu_string_interner_init(heap_t* heap);

lu_string_t* lu_intern_string(string_interner_t* interner, char* str);
