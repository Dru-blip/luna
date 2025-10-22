
#pragma once

#include <stdbool.h>

#include "eval.h"
#include "heap.h"
#include "value.h"

#define LU_EXPORT __attribute__((visibility("default")))

#define LU_ARG_COUNT(state) ((state)->context_stack->call_stack->arg_count)
