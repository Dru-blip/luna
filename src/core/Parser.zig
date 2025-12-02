const std = @import("std");

const Ast = @import("Ast.zig");
const Node = Ast.Node;
const Token = @import("Tokenizer.zig").Token;
const Tokens = @import("Tokenizer.zig").Tokens;

const Parser = @This();

const ParserError = error{
    SyntaxError,
    UnexpectedToken,
} || std.mem.Allocator.Error;

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

fn atEnd(parser: *Parser) bool {
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

fn expectToken(p: *Parser, tag: Token.Tag) ParserError!*const Token {
    if (p.peek().tag == tag) {
        const token = p.peek();
        p.advance();
        return token;
    }
    return ParserError.UnexpectedToken;
}

pub fn parse(p: *Parser) ParserError!Ast {
    while (!p.atEnd()) {
        const stmt = try p.parseStmt();
        try p.ast.nodes.append(p.gpa, stmt);
    }
    return p.ast;
}

fn parseStmt(p: *Parser) ParserError!*Node {
    const token = p.peek();
    switch (token.tag) {
        .keyword_return => return p.parseReturnStmt(),
        else => {
            const expr = try p.parseExpr(0);
            return p.ast.makeExprStmt(expr);
        },
    }
}

fn parseReturnStmt(p: *Parser) ParserError!*Node {
    const token = try p.expectToken(.keyword_return);
    const expr = try p.parseExpr(0);
    return p.ast.makeReturnStmt(token.loc.merge(&expr.loc), expr);
}

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

fn parseExpr(p: *Parser, min_prec: i8) ParserError!*Node {
    var lhs = try p.parsePrimaryExpr();
    while (true) {
        const tok_tag = p.tokenTag(p.tok_i);
        const info = operTable[@as(usize, @intCast(@intFromEnum(tok_tag)))];
        if (info.lbp < min_prec) {
            break;
        }
        _ = p.nextToken();
        const rhs = try p.parseExpr(info.rbp);
        lhs = try p.ast.makeBinOp(info.tag, lhs.loc.merge(&rhs.loc), lhs, rhs);
    }
    return lhs;
}

fn parsePrimaryExpr(p: *Parser) ParserError!*Node {
    const token = p.peek();
    switch (token.tag) {
        .int => {
            p.advance();
            const node = try p.ast.makeNode(.int_literal);
            node.loc = token.loc;
            return node;
        },
        else => {
            return ParserError.SyntaxError;
        },
    }
}
