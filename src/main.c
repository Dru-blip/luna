
#include <stdio.h>
#include <stdlib.h>

#include "parser/ast.h"
#include "parser/parser.h"

int main() {
    //
    ast_program_t program = parse_program("test.luna", "return 5");
    dump_ast(&program);

    return 0;
}
