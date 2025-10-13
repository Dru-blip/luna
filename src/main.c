#include "parser/ast.h"
#include "parser/parser.h"
#include "runtime/eval.h"
#include "runtime/istate.h"

int main(int argc, char* argv[]) {
    //
    lu_istate_t* state = lu_istate_new();
    run_program(state, argv[1]);
    return 0;
}
