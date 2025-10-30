#include "bytecode/interpreter.h"

// TODO: support for if else if.
int main(int argc, char* argv[]) {
    //
    struct lu_istate* state = lu_istate_new();
    lu_run_program(state, argv[1]);
    lu_istate_destroy(state);
    return 0;
}
