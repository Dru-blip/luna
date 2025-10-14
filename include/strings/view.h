
#pragma once

#include <stddef.h>

typedef struct string_view {
    char* str;
    size_t len;
} string_view_t;
