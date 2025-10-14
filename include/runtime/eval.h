#pragma once

#include "istate.h"
#include "runtime/object.h"

void lu_eval_program(lu_istate_t* state);
lu_object_t* lu_call_function(lu_istate_t* state, lu_object_t* self);
lu_object_t* lu_run_program(lu_istate_t* state, const char* filepath);

// TODO: implement
lu_object_t* lu_run_string(lu_istate_t* state, const char* src_string);
