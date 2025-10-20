#pragma once

#include <stddef.h>
#include <stdint.h>

#include "eval.h"

enum lu_value_type {
    VALUE_BOOL,
    VALUE_NONE,
    VALUE_UNDEFINED,
    VALUE_INTEGER,
    VALUE_OBJECT,
};

struct lu_value {
    enum lu_value_type type;
    union {
        int64_t integer;
    };
};
