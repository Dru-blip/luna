#include "parser/parser.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "operator.h"
#include "parser/ast.h"
#include "parser/tokenizer.h"
#include "stb_ds.h"

static ast_node_t* parse_stmt(parser_t* parser);

static ast_node_t* parse_decl(parser_t* parser);
static void parse_param_list(parser_t* parser, ast_node_t*** params);
static ast_node_t* parse_expression(parser_t* parser, int8_t mbp);

static operator_t operators[] = {
    {operator_kind_infix, token_kind_pipe_pipe, 15, 16, binary_op_lor,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_ampersand_ampersand, 18, 19,
     binary_op_land, ast_node_kind_binop},

    {operator_kind_infix, token_kind_equal_equal, 35, 36, binary_op_eq,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_bang_equal, 35, 36, binary_op_neq,
     ast_node_kind_binop},

    {operator_kind_infix, token_kind_less, 40, 41, binary_op_lt,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_less_equal, 40, 41, binary_op_lte,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_greater, 40, 41, binary_op_gt,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_greater_equal, 40, 41, binary_op_gte,
     ast_node_kind_binop},

    {operator_kind_infix, token_kind_less_less, 45, 46, binary_op_shl,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_greater_greater, 45, 46, binary_op_shr,
     ast_node_kind_binop},

    {operator_kind_infix, token_kind_plus, 50, 51, binary_op_add,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_minus, 50, 51, binary_op_sub,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_asterisk, 55, 56, binary_op_mul,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_slash, 55, 56, binary_op_div,
     ast_node_kind_binop},
    {operator_kind_infix, token_kind_modulus, 55, 56, binary_op_mod,
     ast_node_kind_binop},
};

static const uint32_t operator_table_len =
    sizeof(operators) / sizeof(operators[0]);

static bool is_valid_operator(operator_t** opinfo, const token_kind_t kind) {
    for (uint32_t i = 0; i < operator_table_len; i++) {
        if (operators[i].token_kind == kind) {
            *opinfo = &operators[i];
            return true;
        }
    }
    return false;
}

static ast_node_t* make_node(parser_t* parser, ast_node_kind_t kind,
                             span_t* span) {
    ast_node_t* node = arena_alloc(&parser->allocator, sizeof(ast_node_t));
    node->kind = kind;
    node->span = *span;
    return node;
}

static uint32_t parser_advance(parser_t* parser) {
    if (parser->pos + 1 <= arrlen(parser->token_list)) {
        parser->cur_token = &parser->token_list[parser->pos];
        return parser->pos++;
    }
    return parser->pos;
}

static token_t* current_token(const parser_t* parser) {
    return parser->cur_token;
}

static token_t* parser_eat(parser_t* parser) {
    token_t* tok = current_token(parser);
    parser_advance(parser);
    return tok;
}

static token_t* parse_expected(parser_t* parser, const token_kind_t kind) {
    if (parser->cur_token->kind != kind) {
        printf("expected %s\n", token_kind_labels[kind]);
        exit(EXIT_FAILURE);
    }
    token_t* token = parser_eat(parser);
    return token;
}

static bool check(const parser_t* parser, const token_kind_t kind) {
    return parser->cur_token->kind == kind;
}

static ast_node_t* parse_primary_expression(parser_t* parser) {
    token_t* token = parser_eat(parser);

    switch (token->kind) {
        case token_kind_integer: {
            ast_node_t* node =
                make_node(parser, ast_node_kind_int, &token->span);
            node->data.int_val = token->data.int_val;
            return node;
        }
        case token_kind_keyword_true:
        case token_kind_keyword_false: {
            ast_node_t* node =
                make_node(parser, ast_node_kind_bool, &token->span);
            node->data.int_val = token->kind == token_kind_keyword_true;
            return node;
        }
        default: {
            printf("invalid syntax\n");
            exit(EXIT_FAILURE);
        }
    }
}

static ast_node_t* parse_prefix_expression(parser_t* parser) {
    token_t* token = current_token(parser);
    switch (token->kind) {
        case token_kind_plus:
        case token_kind_minus: {
            parser_advance(parser);
            ast_node_t* argument = parse_expression(parser, 0);
            ast_node_t* node =
                make_node(parser, ast_node_kind_unop, &token->span);
            node->data.unop = (ast_unop_t){
                .op = token->kind == token_kind_plus ? unary_op_plus
                                                     : unary_op_minus,
                .is_prefix = true,
                .argument = argument,
            };
            return node;
        }
        case token_kind_bang: {
            parser_advance(parser);
            ast_node_t* argument = parse_expression(parser, 0);
            ast_node_t* node =
                make_node(parser, ast_node_kind_unop, &token->span);
            node->data.unop = (ast_unop_t){
                .op = unary_op_lnot,
                .is_prefix = true,
                .argument = argument,
            };
            return node;
        }
        default: {
            return parse_primary_expression(parser);
        }
    }
}

static ast_node_t* parse_expression(parser_t* parser, int8_t mbp) {
    ast_node_t* lhs = parse_prefix_expression(parser);
    while (true) {
        operator_t* op = nullptr;
        token_t* token = current_token(parser);
        if (!is_valid_operator(&op, token->kind)) break;

        if (op->lbp < mbp) break;
        parser_advance(parser);
        ast_node_t* rhs = parse_expression(parser, op->rbp);
        ast_node_t* bin = make_node(parser, op->node_kind, &token->span);
        bin->data.binop = (ast_binop_t){
            .lhs = lhs,
            .op = op->op,
            .rhs = rhs,
        };
        lhs = bin;
    }
    return lhs;
}

static ast_node_t* parse_return(parser_t* parser) {
    token_t* ret_token = parser_eat(parser);
    ast_node_t* node =
        make_node(parser, ast_node_kind_return, &ret_token->span);
    node->data.node = parse_expression(parser, 0);
    // parse_expected(parser, token_kind_semicolon);
    return node;
}

static ast_node_t* parse_stmt(parser_t* parser) {
    const token_t* token = parser->cur_token;
    switch (token->kind) {
        case token_kind_keyword_return: {
            return parse_return(parser);
        }
        default: {
            // const uint32_t token_index = parser->pos;
            // ast_node_t* expr = parse_expression(parser, 0);
            // ast_node_t* node =
            //     make_node(parser, ast_node_kind_expr_stmt, token_index);
            // node->data.node = expr;
            // return node;
        }
    }
}

ast_program_t parse_program(const char* filename, const char* source) {
    token_t* tokens = tokenize(source);

    parser_t parser = {
        .source = source,
        .token_list = tokens,
        .filename = filename,
        .pos = 0,
    };

    arena_init(&parser.allocator);

    parser_advance(&parser);
    ast_node_t** nodes = nullptr;
    while (!check(&parser, token_kind_eof)) {
        ast_node_t* node = parse_stmt(&parser);
        arrput(nodes, node);
    }

    return (ast_program_t){
        .allocator = parser.allocator,
        .nodes = nodes,
        .tokens = tokens,
        .source = source,
        .filepath = filename,
    };
}
