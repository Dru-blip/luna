#pragma once

#include "parser/tokenizer.h"
#include "runtime/istate.h"
#include "runtime/object.h"
#include "runtime/objects/strobj.h"

typedef struct lu_error {
    LUNA_OBJECT_HEADER;
    size_t line;
    span_t span;
    char* message;
    lu_string_t* msg;
} lu_error_t;

lu_error_t* lu_error_from_str(lu_istate_t* state, char* message);
lu_error_t* lu_error_from_str_with_span(lu_istate_t* state, char* message,
                                        span_t* span);

void lu_print_error(lu_error_t* error);
