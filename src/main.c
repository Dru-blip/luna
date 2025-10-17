#include <threads.h>

#include "runtime/eval.h"
#include "runtime/istate.h"

//   TODO: implement base type object or base object class
//   TODO: string operations
//   TODO: unary operators
//   TODO: error objects and formatted printers and buffers.
//   TODO: binary ops between int and bool
//   TODO: implement garbage collection
int main(int argc, char* argv[]) {
    lu_istate_t* state = lu_istate_new();
    lu_run_program(state, argv[1]);
    lu_istate_destroy(state);
    return 0;
}
