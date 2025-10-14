#pragma once

#include "runtime/objects/strobj.h"

struct string_interner;

typedef enum {
    fly_string_node_red,
    fly_string_node_black,
} fly_string_node_color;

typedef struct fly_string_node {
    struct fly_string_node *left, *right, *parent;
    lu_string_t* str;
    fly_string_node_color color;
} fly_string_node_t;

lu_string_t* fly_string_insert(struct string_interner* interner, char* str);
