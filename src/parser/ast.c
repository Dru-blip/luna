#include "parser/ast.h"

#include <stdint.h>
#include <stdio.h>

#include "stb_ds.h"

void dump_node(const ast_node_t* node, uint32_t indent) {
    switch (node->kind) {
        case ast_node_kind_int: {
            printf("%*sint: %ld\n", indent, "", node->data.int_val);
            break;
        }
        case ast_node_kind_bool: {
            printf("%*sint: %s\n", indent, "",
                   node->data.int_val ? "true" : "false");
            break;
        }
        case ast_node_kind_unop: {
            printf("%*sunop: %d\n", "", node->data.unop.op);
            dump_node(node->data.unop.argument, indent + 2);
            break;
        }
        case ast_node_kind_binop: {
            printf("%*sbinop: %d\n", indent, "", node->data.binop.op);
            dump_node(node->data.binop.lhs, indent + 2);
            dump_node(node->data.binop.rhs, indent + 2);
            break;
        }
        case ast_node_kind_return: {
            printf("%*sreturn: \n", indent, "");
            dump_node(node->data.node, indent + 2);
            break;
        }
        case ast_node_kind_block: {
            break;
        }
        case ast_node_kind_if_stmt: {
            break;
        }
        default: {
            printf("unknown node kind\n");
            break;
        }
    }
}

void dump_nodes(const ast_node_t** nodes, uint32_t indent) {
    const uint32_t nnodes = arrlen(nodes);
    for (uint32_t i = 0; i < nnodes; i++) {
        dump_node(nodes[i], indent);
    }
}

void dump_ast(const ast_program_t* program) {
    printf("Program:\n");
    dump_nodes(program->nodes, 2);
}
