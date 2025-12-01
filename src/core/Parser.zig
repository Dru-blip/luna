const std = @import("std");

const Ast = @import("Ast.zig");
const Node = Ast.Node;
const Token = @import("Tokenizer.zig").Token;
const Tokens = @import("Tokenizer.zig").Tokens;

const Parser = @This();

source: []const u8,
tok_i: usize,
ast: Ast,
tokens: Tokens,

pub fn init(gpa: std.mem.Allocator, source: []const u8, tokens: Tokens) Parser {
    return .{
        .source = source,
        .tok_i = 0,
        .ast = Ast.init(gpa),
        .tokens = tokens,
    };
}

fn advance(parser: *Parser) void {
    if (parser.tok_i + 1 < parser.tokens.items.len) {
        parser.tok_i += 1;
    }
}

fn peek(parser: *Parser) *const Token {
    return &parser.tokens.items[parser.tok_i];
}

fn parse_primary_expr(parser: *Parser) !*Node {
    return try parser.ast.make_node(.int_literal);
}

fn parse_expr(parser: *Parser) !*Node {
    const lhs = try parse_primary_expr(parser);
    return lhs;
}
