#include "runtime/eval.h"
#include "runtime/istate.h"

int main(int argc, char* argv[]) {
    lu_istate_t* state = lu_istate_new();
    lu_run_program(state, argv[1]);
    lu_istate_destroy(state);
    return 0;
}
