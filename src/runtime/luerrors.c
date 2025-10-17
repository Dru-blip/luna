#include "runtime/luerrors.h"

#include <stdio.h>
#include <string.h>

#include "runtime/heap.h"
#include "strings/interner.h"

lu_error_t* lu_error_from_str(lu_istate_t* state, char* message) {
    lu_error_t* error = heap_allocate_object(state->heap, sizeof(lu_error_t));
    error->msg = lu_intern_string(state->string_pool, message, strlen(message));
    return error;
}

lu_error_t* lu_error_from_str_with_span(lu_istate_t* state, char* message,
                                        span_t* span) {
    lu_error_t* error = heap_allocate_object(state->heap, sizeof(lu_error_t));
    error->msg = lu_intern_string(state->string_pool, message, strlen(message));
    error->span = *span;
    error->line = span->line;
    return error;
}

void lu_print_error(lu_error_t* error) {
    printf("Error: %.*s at line %ld\n", error->msg->data.len,
           error->msg->data.str, error->span.line);
}
