#include "bytecode/interpreter.h"

int main(int argc, char* argv[]) {
    struct lu_istate* state = lu_istate_new();
    lu_istate_process_init(state, argc, argv);
    lu_run_program(state, argv[1]);
    lu_istate_destroy(state);
    return 0;
}
