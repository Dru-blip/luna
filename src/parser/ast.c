#include "ast.h"

#include <stdint.h>
#include <stdio.h>

#include "stb_ds.h"

void dump_node(const struct ast_node* node, uint32_t indent) {
    switch (node->kind) {
        case AST_NODE_INT: {
            printf("%*sint: %ld\n", indent, "", node->data.int_val);
            break;
        }
        case AST_NODE_BOOL: {
            printf("%*sint: %s\n", indent, "",
                   node->data.int_val ? "true" : "false");
            break;
        }
        case AST_NODE_UNOP: {
            printf("%*sunop: %d\n", "", node->data.unop.op);
            dump_node(node->data.unop.argument, indent + 2);
            break;
        }
        case AST_NODE_BINOP: {
            printf("%*sbinop: %d\n", indent, "", node->data.binop.op);
            dump_node(node->data.binop.lhs, indent + 2);
            dump_node(node->data.binop.rhs, indent + 2);
            break;
        }
        case AST_NODE_RETURN: {
            printf("%*sreturn: \n", indent, "");
            dump_node(node->data.node, indent + 2);
            break;
        }
        case AST_NODE_BLOCK: {
            break;
        }
        case AST_NODE_IF_STMT: {
            break;
        }
        default: {
            printf("unknown node kind\n");
            break;
        }
    }
}

void dump_nodes(struct ast_node** nodes, uint32_t indent) {
    const uint32_t nnodes = arrlen(nodes);
    for (uint32_t i = 0; i < nnodes; i++) {
        dump_node(nodes[i], indent);
    }
}

void dump_ast(const struct ast_program* program) {
    printf("Program:\n");
    dump_nodes(program->nodes, 2);
}
