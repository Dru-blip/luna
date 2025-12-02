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
gpa: std.mem.Allocator,

pub fn init(gpa: std.mem.Allocator, source: []const u8, tokens: Tokens) Parser {
    return .{
        .source = source,
        .tok_i = 0,
        .ast = Ast.init(gpa),
        .gpa = gpa,
        .tokens = tokens,
    };
}

fn advance(parser: *Parser) void {
    if (parser.tok_i < parser.tokens.items.len) {
        parser.tok_i += 1;
    }
}

fn at_end(parser: *Parser) bool {
    return parser.tokens.items[parser.tok_i].tag == .eof;
}

fn peek(parser: *Parser) *const Token {
    return &parser.tokens.items[parser.tok_i];
}

fn tokenTag(p: *Parser, tok_i: usize) Token.Tag {
    return p.tokens.items[tok_i].tag;
}

fn nextToken(p: *Parser) *const Token {
    p.advance();
    return &p.tokens.items[p.tok_i];
}

pub fn parse(p: *Parser) !Ast {
    while (!p.at_end()) {
        const stmt = try p.parse_stmt();
        if (stmt) |node| {
            try p.ast.nodes.append(p.gpa, node);
        }
    }
    return p.ast;
}

fn parse_stmt(p: *Parser) !?*Node {
    const token = p.peek();
    switch (token.tag) {
        else => {
            //TODO: make a utility function to create nodes.
            const expr = try p.parse_expr(0);
            var node = try p.ast.make_node(.expr_stmt);
            node.data.un = expr.?;
            return node;
        },
    }
}

const Assoc = enum {
    left,
    none,
};

const OperInfo = struct {
    lbp: i8,
    rbp: i8,
    tag: Node.Tag,
};

const operTable = std.enums.directEnumArrayDefault(Token.Tag, OperInfo, .{ .lbp = -1, .rbp = -1, .tag = Node.Tag.root }, 0, .{
    .plus = .{ .lbp = 60, .rbp = 61, .tag = .add },
    .minus = .{ .lbp = 60, .rbp = 61, .tag = .sub },
    .asterisk = .{ .lbp = 70, .rbp = 71, .tag = .mul },
    .slash = .{ .lbp = 70, .rbp = 71, .tag = .div },
});

fn parse_expr(p: *Parser, min_prec: i8) !?*Node {
    var lhs = try p.parse_primary_expr();
    while (true) {
        const tok_tag = p.tokenTag(p.tok_i);
        const info = operTable[@as(usize, @intCast(@intFromEnum(tok_tag)))];
        if (info.lbp < min_prec) {
            break;
        }

        const oper_token = p.nextToken();
        const rhs = try p.parse_expr(info.rbp);
        const node = try p.ast.make_node(info.tag);
        node.loc = oper_token.loc;
        node.data.bin = .{ .lhs = lhs.?, .rhs = rhs.? };
        lhs = node;
    }
    return lhs;
}

fn parse_primary_expr(p: *Parser) !?*Node {
    const token = p.peek();
    switch (token.tag) {
        .int => {
            p.advance();
            const node = try p.ast.make_node(.int_literal);
            node.loc = token.loc;
            return node;
        },
        else => unreachable,
    }
    return null;
}
