#pragma once

#include "istate.h"
#include "runtime/object.h"

void eval_program(lu_istate_t* state);
lu_object_t* eval_call(lu_istate_t* state, lu_object_t* this);
lu_object_t* run_program(lu_istate_t* state, const char* filepath);

//TODO: implement
lu_object_t* lu_run_string(lu_istate_t* state, const char* src_string);
