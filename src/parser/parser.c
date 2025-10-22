#include "parser.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "ast.h"
#include "operator.h"
#include "stb_ds.h"
#include "tokenizer.h"

static struct ast_node* parse_stmt(struct parser* parser);
static void parse_param_list(struct parser* parser, struct ast_node*** params);
static struct ast_node* parse_expression(struct parser* parser, int8_t mbp);

static struct operator operators[] = {
    {OPERATOR_ASSIGN, TOKEN_EQUAL, 1, 1, OP_ASSIGN_SIMPLE, AST_NODE_ASSIGN},

    {OPERATOR_INFIX, TOKEN_PIPE_PIPE, 15, 16, OP_LOR, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_AMPERSAND_AMPERSAND, 18, 19, OP_LAND,
     AST_NODE_BINOP},

    {OPERATOR_INFIX, TOKEN_EQUAL_EQUAL, 35, 36, OP_EQ, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_BANG_EQUAL, 35, 36, OP_NEQ, AST_NODE_BINOP},

    {OPERATOR_INFIX, TOKEN_LESS, 40, 41, OP_LT, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_LESS_EQUAL, 40, 41, OP_LTE, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_GREATER, 40, 41, OP_GT, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_GREATER_EQUAL, 40, 41, OP_GTE, AST_NODE_BINOP},

    {OPERATOR_INFIX, TOKEN_LESS_LESS, 45, 46, OP_SHL, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_GREATER_GREATER, 45, 46, OP_SHR, AST_NODE_BINOP},

    {OPERATOR_INFIX, TOKEN_PLUS, 50, 51, OP_ADD, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_MINUS, 50, 51, OP_SUB, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_ASTERISK, 55, 56, OP_MUL, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_SLASH, 55, 56, OP_DIV, AST_NODE_BINOP},
    {OPERATOR_INFIX, TOKEN_MODULUS, 55, 56, OP_MOD, AST_NODE_BINOP},

    {OPERATOR_POSTFIX, TOKEN_LPAREN, 99, 100, OP_CALL, AST_NODE_CALL},
    {OPERATOR_POSTFIX, TOKEN_DOT, 99, 100, OP_MEMBER, AST_NODE_MEMBER_EXPR},
    {OPERATOR_POSTFIX, TOKEN_LBRACKET, 99, 100, OP_COMPUTED_MEMBER,
     AST_NODE_COMPUTED_MEMBER_EXPR},
};

static const uint32_t operator_table_len =
    sizeof(operators) / sizeof(operators[0]);

static bool is_valid_operator(struct operator** opinfo, enum token_kind kind) {
    for (uint32_t i = 0; i < operator_table_len; i++) {
        if (operators[i].token_kind == kind) {
            *opinfo = &operators[i];
            return true;
        }
    }
    return false;
}

static struct ast_node* make_node(struct parser* parser,
                                  enum ast_node_kind kind, struct span span) {
    struct ast_node* node =
        arena_alloc(&parser->allocator, sizeof(struct ast_node));
    node->kind = kind;
    node->span = span;
    return node;
}

static uint32_t parser_advance(struct parser* parser) {
    if (parser->pos + 1 <= arrlen(parser->token_list)) {
        parser->cur_token = &parser->token_list[parser->pos];
        return parser->pos++;
    }
    return parser->pos;
}

static struct token* current_token(const struct parser* parser) {
    return parser->cur_token;
}

static struct token* parser_eat(struct parser* parser) {
    struct token* tok = current_token(parser);
    parser_advance(parser);
    return tok;
}

static struct token* parse_expected(struct parser* parser,
                                    enum token_kind kind) {
    if (parser->cur_token->kind != kind) {
        printf("expected %s\n", token_labels[kind]);
        exit(EXIT_FAILURE);
    }
    return parser_eat(parser);
}

static bool check(const struct parser* parser, enum token_kind kind) {
    return parser->cur_token->kind == kind;
}

static struct ast_node* parse_primary_expression(struct parser* parser) {
    struct token* token = parser_eat(parser);

    switch (token->kind) {
        case TOKEN_INTEGER: {
            struct ast_node* node =
                make_node(parser, AST_NODE_INT, token->span);
            node->data.int_val = token->data.int_val;
            return node;
        }
        case TOKEN_KEYWORD_TRUE:
        case TOKEN_KEYWORD_FALSE: {
            struct ast_node* node =
                make_node(parser, AST_NODE_BOOL, token->span);
            node->data.int_val = token->kind == TOKEN_KEYWORD_TRUE;
            return node;
        }
        case TOKEN_IDENTIFIER: {
            struct ast_node* node =
                make_node(parser, AST_NODE_IDENTIFIER, token->span);
            return node;
        }
        case TOKEN_STRING: {
            struct ast_node* node =
                make_node(parser, AST_NODE_STR, token->span);
            node->data.id = token->data.str_val;
            return node;
        }
        default: {
            fprintf(stderr, "invalid syntax\n");
            exit(EXIT_FAILURE);
        }
    }
}

static struct ast_node* parse_prefix_expression(struct parser* parser) {
    struct token* token = current_token(parser);
    switch (token->kind) {
        case TOKEN_PLUS:
        case TOKEN_MINUS: {
            parser_advance(parser);
            struct ast_node* argument = parse_expression(parser, 0);
            struct ast_node* node = make_node(
                parser, AST_NODE_UNOP, SPAN_MERGE(token->span, argument->span));
            node->data.unop = (struct ast_unop){
                .op = token->kind == TOKEN_PLUS ? OP_PLUS : OP_MINUS,
                .is_prefix = true,
                .argument = argument,
            };
            return node;
        }
        case TOKEN_BANG: {
            parser_advance(parser);
            struct ast_node* argument = parse_expression(parser, 0);
            struct ast_node* node = make_node(
                parser, AST_NODE_UNOP, SPAN_MERGE(token->span, argument->span));
            node->data.unop = (struct ast_unop){
                .op = OP_LNOT,
                .is_prefix = true,
                .argument = argument,
            };
            return node;
        }
        case TOKEN_LBRACKET: {
            struct token* token = parser_eat(parser);
            struct ast_node** elements = nullptr;
            while (!check(parser, TOKEN_RBRACKET)) {
                struct ast_node* element = parse_expression(parser, 0);
                if (check(parser, TOKEN_COMMA)) {
                    parser_advance(parser);
                }
                arrput(elements, element);
            }
            struct ast_node* node =
                make_node(parser, AST_NODE_ARRAY_EXPR,
                          SPAN_MERGE(token->span, parser->cur_token->span));
            parse_expected(parser, TOKEN_RBRACKET);
            node->data.list = elements;
            return node;
        }
        default: {
            return parse_primary_expression(parser);
        }
    }
}

static void parse_call_args(struct parser* parser, uint8_t* argc,
                            struct ast_node*** args) {
    while (!check(parser, TOKEN_RPAREN)) {
        struct ast_node* arg = parse_expression(parser, 0);
        if (check(parser, TOKEN_COMMA)) {
            parser_advance(parser);
        }
        arrput(*args, arg);
        (*argc)++;
    }
}

static struct ast_node* parse_postfix_expression(struct parser* parser,
                                                 enum postfix_op op,
                                                 struct token* token,
                                                 struct ast_node* lhs) {
    if (op == OP_CALL) {
        struct ast_node** args = nullptr;
        uint8_t argc = 0;
        parse_call_args(parser, &argc, &args);
        struct ast_node* call =
            make_node(parser, AST_NODE_CALL,
                      SPAN_MERGE(lhs->span, parser->cur_token->span));
        call->data.call = (struct ast_call){
            .callee = lhs,
            .argc = argc,
            .args = args,
        };
        parser_advance(parser); /* consume ')' */
        return call;
    }
    if (op == OP_MEMBER) {
        struct token* property_token = parse_expected(parser, TOKEN_IDENTIFIER);
        struct ast_node* member_expr =
            make_node(parser, AST_NODE_MEMBER_EXPR,
                      SPAN_MERGE(lhs->span, property_token->span));
        member_expr->data.member_expr = (struct ast_member_expr){
            .object = lhs,
            .property_name = property_token->span,
        };
        return member_expr;
    }

    if (op == OP_COMPUTED_MEMBER) {
        struct ast_node* property = parse_expression(parser, 0);
        struct ast_node* member_expr =
            make_node(parser, AST_NODE_COMPUTED_MEMBER_EXPR,
                      SPAN_MERGE(lhs->span, parser->cur_token->span));
        parse_expected(parser, TOKEN_RBRACKET);
        member_expr->data.pair = (struct ast_pair){
            .fst = lhs,
            .snd = property,
        };
        return member_expr;
    }

    return lhs;
}

static struct ast_node* parse_expression(struct parser* parser, int8_t mbp) {
    struct ast_node* lhs = parse_prefix_expression(parser);
    while (true) {
        struct operator* op = nullptr;
        struct token* token = current_token(parser);
        if (!is_valid_operator(&op, token->kind)) break;

        if (op->lbp < mbp) break;
        parser_advance(parser);
        if (op->kind == OPERATOR_POSTFIX) {
            lhs = parse_postfix_expression(parser, op->op, token, lhs);
            continue;
        }
        struct ast_node* rhs = parse_expression(parser, op->rbp);
        struct ast_node* bin =
            make_node(parser, op->node_kind, SPAN_MERGE(lhs->span, rhs->span));
        bin->data.binop = (struct ast_binop){
            .lhs = lhs,
            .op = op->op,
            .rhs = rhs,
        };
        lhs = bin;
    }
    return lhs;
}

static struct ast_node* parse_return(struct parser* parser) {
    struct token* ret_token = parser_eat(parser);
    struct ast_node* expr = parse_expression(parser, 0);
    struct ast_node* node = make_node(parser, AST_NODE_RETURN,
                                      SPAN_MERGE(ret_token->span, expr->span));
    node->data.node = expr;
    return node;
}

static struct ast_node* parse_block(struct parser* parser) {
    struct token* token = parse_expected(parser, TOKEN_LBRACE);
    struct ast_node** stmts = nullptr;
    while (!check(parser, TOKEN_RBRACE)) {
        if (check(parser, TOKEN_EOF)) exit(EXIT_FAILURE);
        struct ast_node* stmt = parse_stmt(parser);
        arrput(stmts, stmt);
    }
    struct ast_node* node =
        make_node(parser, AST_NODE_BLOCK,
                  SPAN_MERGE(token->span, parser->cur_token->span));
    node->data.list = stmts;
    parse_expected(parser, TOKEN_RBRACE);
    return node;
}

static struct ast_node* parse_if_stmt(struct parser* parser) {
    struct token* token = parser_eat(parser);
    if (check(parser, TOKEN_LPAREN)) parser_advance(parser);
    struct ast_node* test = parse_expression(parser, 0);
    if (check(parser, TOKEN_RPAREN)) parser_advance(parser);
    struct ast_node* consequent = parse_stmt(parser);
    struct ast_node* alternate = nullptr;
    if (check(parser, TOKEN_KEYWORD_ELSE)) {
        parser_advance(parser);
        if (check(parser, TOKEN_EOF)) exit(EXIT_FAILURE);
        alternate = parse_stmt(parser);
    }
    struct ast_node* node =
        make_node(parser, AST_NODE_IF_STMT,
                  SPAN_MERGE(token->span, parser->cur_token->span));
    node->data.if_stmt = (struct ast_if_stmt){
        .test = test,
        .consequent = consequent,
        .alternate = alternate,
    };
    return node;
}

static struct ast_node* parse_break_stmt(struct parser* parser) {
    struct token* token = parser_eat(parser);
    return make_node(parser, AST_NODE_BREAK_STMT, token->span);
}

static struct ast_node* parse_continue_stmt(struct parser* parser) {
    struct token* token = parser_eat(parser);
    return make_node(parser, AST_NODE_CONTINUE_STMT, token->span);
}

static struct ast_node* parse_loop_stmt(struct parser* parser) {
    struct token* token = parser_eat(parser);
    struct ast_node* body = parse_stmt(parser);
    struct ast_node* node = make_node(parser, AST_NODE_LOOP_STMT,
                                      SPAN_MERGE(token->span, body->span));
    node->data.node = body;
    return node;
}

static struct ast_node* parse_while_stmt(struct parser* parser) {
    struct token* token = parser_eat(parser);
    if (check(parser, TOKEN_LPAREN)) parser_advance(parser);
    struct ast_node* test = parse_expression(parser, 0);
    if (check(parser, TOKEN_RPAREN)) parser_advance(parser);
    struct ast_node* body = parse_stmt(parser);
    struct ast_node* node = make_node(parser, AST_NODE_WHILE_STMT,
                                      SPAN_MERGE(token->span, body->span));
    node->data.pair = (struct ast_pair){.fst = test, .snd = body};
    return node;
}

static struct ast_node* parse_for_stmt(struct parser* parser) {
    struct token* token = parser_eat(parser);
    parse_expected(parser, TOKEN_LPAREN);
    struct ast_node* init = parse_stmt(parser);
    parse_expected(parser, TOKEN_SEMICOLON);
    struct ast_node* test = parse_expression(parser, 0);
    parse_expected(parser, TOKEN_SEMICOLON);
    struct ast_node* update = parse_expression(parser, 0);
    parse_expected(parser, TOKEN_RPAREN);
    struct ast_node* body = parse_stmt(parser);
    struct ast_node* node = make_node(parser, AST_NODE_FOR_STMT,
                                      SPAN_MERGE(token->span, body->span));
    node->data.for_stmt = (struct ast_for_stmt){
        .init = init,
        .test = test,
        .update = update,
        .body = body,
    };
    return node;
}

static void parse_param_list(struct parser* parser, struct ast_node*** params) {
    parse_expected(parser, TOKEN_LPAREN);
    while (!check(parser, TOKEN_RPAREN)) {
        struct token* param_id = parse_expected(parser, TOKEN_IDENTIFIER);
        struct ast_node* param =
            make_node(parser, AST_NODE_PARAM, param_id->span);
        if (check(parser, TOKEN_COMMA)) parser_advance(parser);
        arrput(*params, param);
    }
    parse_expected(parser, TOKEN_RPAREN);
}

static struct ast_node* parse_fn_decl(struct parser* parser) {
    struct token* token = parser_eat(parser);
    struct token* id = parse_expected(parser, TOKEN_IDENTIFIER);
    struct ast_node** params = nullptr;
    parse_param_list(parser, &params);
    struct ast_node* body = parse_stmt(parser);
    struct ast_node* node = make_node(parser, AST_NODE_FN_DECL,
                                      SPAN_MERGE(token->span, body->span));
    node->data.fn_decl = (struct ast_fn_decl){
        .name_span = id->span,
        .params = params,
        .body = body,
    };
    return node;
}

static struct ast_node* parse_stmt(struct parser* parser) {
    struct token* token = parser->cur_token;
    switch (token->kind) {
        case TOKEN_KEYWORD_RETURN:
            return parse_return(parser);
        case TOKEN_LBRACE:
            return parse_block(parser);
        case TOKEN_KEYWORD_IF:
            return parse_if_stmt(parser);
        case TOKEN_KEYWORD_CONTINUE:
            return parse_continue_stmt(parser);
        case TOKEN_KEYWORD_BREAK:
            return parse_break_stmt(parser);
        case TOKEN_KEYWORD_LOOP:
            return parse_loop_stmt(parser);
        case TOKEN_KEYWORD_WHILE:
            return parse_while_stmt(parser);
        case TOKEN_KEYWORD_FOR:
            return parse_for_stmt(parser);
        case TOKEN_KEYWORD_FN:
            return parse_fn_decl(parser);
        default: {
            struct ast_node* expr = parse_expression(parser, 0);
            struct ast_node* node =
                make_node(parser, AST_NODE_EXPR_STMT,
                          SPAN_MERGE(token->span, expr->span));
            node->data.node = expr;
            return node;
        }
    }
}

struct ast_program parse_program(const char* filename, const char* source) {
    struct token* tokens = tokenize(source);

    struct parser parser = {
        .source = source,
        .token_list = tokens,
        .filename = filename,
        .pos = 0,
    };

    arena_init(&parser.allocator);
    parser_advance(&parser);

    struct ast_node** nodes = nullptr;
    while (!check(&parser, TOKEN_EOF)) {
        struct ast_node* node = parse_stmt(&parser);
        arrput(nodes, node);
    }

    return (struct ast_program){
        .allocator = parser.allocator,
        .nodes = nodes,
        .tokens = tokens,
        .source = source,
        .filepath = filename,
    };
}
