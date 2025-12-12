const std = @import("std");

const Ast = @import("Ast.zig");
const Node = Ast.Node;
const Token = @import("Tokenizer.zig").Token;
const Tokens = @import("Tokenizer.zig").Tokens;

const Parser = @This();

pub const ParserError = error{
    SyntaxError,
    UnexpectedToken,
    UnexpectedEOF,
} || std.mem.Allocator.Error || std.fmt.ParseIntError || std.Io.Writer.Error;

const Error = struct {
    message: []const u8,
    location: Token.Loc,
};

source: [:0]const u8,
tok_i: usize,
ast: Ast,
tokens: Tokens,
gpa: std.mem.Allocator,
errors: std.ArrayList(Error) = .empty,

pub fn init(gpa: std.mem.Allocator, source: [:0]const u8, tokens: Tokens) Parser {
    return .{
        .source = source,
        .tok_i = 0,
        .ast = Ast.init(gpa, source),
        .gpa = gpa,
        .tokens = tokens,
    };
}

fn recordError(p: *Parser, location: Token.Loc, comptime format: []const u8, args: anytype) !void {
    const message = try std.fmt.allocPrint(p.gpa, format, args);
    try p.errors.append(p.gpa, .{
        .message = message,
        .location = location,
    });
}

// static void extract_source_line_info(struct lu_istate* state,
//                                      struct span* loc,
//                                      size_t* line_start_offset,
//                                      size_t* line_length) {
//     int64_t line_start = loc->start, line_end = loc->end;
//     const char* source = state->running_module->program.source;
//     size_t source_length = state->running_module->program.source_length;
//     while (line_start > 0 && source[line_start - 1] != '\n')
//         line_start--;
//     while (line_end < source_length && source[line_end] != '\n')
//         line_end++;
//     *line_length = line_end - line_start;
//     *line_start_offset = line_start;
// }

fn extract_source_line(
    source: []const u8,
    loc: *const Token.Loc,
    line_start_offset: *usize,
    line_length: *usize,
) void {
    var start = loc.start;
    var end = loc.end;
    while (start > 0 and source[start - 1] != '\n')
        start -= 1;
    while (end < source.len and source[end] != '\n')
        end += 1;
    line_start_offset.* = start;
    line_length.* = end - start;
}

fn renderErrors(p: *Parser) !void {
    var buffer: [256]u8 = undefined;
    var stdout_writer = std.fs.File.stdout().writer(&buffer);
    var stdout = &stdout_writer.interface;
    for (p.errors.items) |err| {
        var line_start: usize = 0;
        var line_length: usize = 0;
        extract_source_line(p.source, &err.location, &line_start, &line_length);
        try stdout.print("SyntaxError: {s}\n", .{err.message});
        try stdout.print("   ", .{});
        try stdout.print("{s}\n", .{p.source[line_start .. line_start + line_length]});
        try stdout.print("   ", .{});
        for (0..(err.location.start - line_start)) |_| {
            try stdout.print(" ", .{});
        }
        for (0..(err.location.end - err.location.start)) |_| {
            try stdout.print("^", .{});
        }
        try stdout.print("\n", .{});
    }
    try stdout.flush();
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

    try p.recordError(
        p.peek().loc,
        "Expected '{s}' but found '{s}'",
        .{
            @tagName(tag),
            @tagName(p.peek().tag),
        },
    );
    return ParserError.UnexpectedToken;
}

pub fn parse(p: *Parser) ParserError!Ast {
    while (!p.atEnd()) {
        const stmt = p.parseStmt() catch |err| {
            try p.renderErrors();
            //TODO: should try to synchronize the parser instead of early exit on single parser error.
            return err;
        };
        try p.ast.nodes.append(p.gpa, stmt);
    }
    p.tokens.deinit(p.gpa);
    return p.ast;
}

fn parseStmt(p: *Parser) ParserError!*Node {
    const token = p.peek();
    switch (token.tag) {
        .keyword_let => return p.parseLetDecl(),
        .l_brace => return try p.parseBlockStmt(),
        .keyword_if => return try p.parseIfStmt(),
        .keyword_return => return p.parseReturnStmt(),
        .keyword_loop => return p.parseLoopStmt(),
        .keyword_break => return p.parseBreakStmt(),
        .keyword_continue => return p.parseContinueStmt(),
        .keyword_while => return p.parseWhileStmt(),
        .keyword_for => return p.parseForStmt(),
        else => {
            const expr = try p.parseExpr(0);
            return p.ast.makeExprStmt(expr);
        },
    }
}

fn parseLetDecl(p: *Parser) ParserError!*Node {
    const token = try p.expectToken(.keyword_let);
    const name = try p.expectToken(.identifier);
    _ = try p.expectToken(.equal);
    const expr = try p.parseExpr(0);
    return p.ast.makeLetDecl(token.loc.merge(&expr.loc), p.source[name.loc.start..name.loc.end], expr);
}

fn parseForStmt(p: *Parser) ParserError!*Node {
    const token = try p.expectToken(.keyword_for);
    const initializer = try p.parseStmt();
    _ = try p.expectToken(.semicolon);
    const @"test" = try p.parseExpr(0);
    _ = try p.expectToken(.semicolon);
    const update = try p.parseExpr(0);
    const body = try p.parseStmt();
    return p.ast.makeForStmt(token.loc.merge(&body.loc), initializer, @"test", update, body);
}

fn parseWhileStmt(p: *Parser) ParserError!*Node {
    const token = try p.expectToken(.keyword_while);
    const @"test" = try p.parseExpr(0);
    const body = try p.parseStmt();
    return p.ast.makeWhileStmt(token.loc.merge(&body.loc), @"test", body);
}

fn parseLoopStmt(p: *Parser) ParserError!*Node {
    const token = try p.expectToken(.keyword_loop);
    const body = try p.parseBlockStmt();
    return p.ast.makeLoopStmt(token.loc.merge(&body.loc), body);
}

fn parseBreakStmt(p: *Parser) ParserError!*Node {
    const token = try p.expectToken(.keyword_break);
    return p.ast.makeBreakStmt(token.loc);
}

fn parseContinueStmt(p: *Parser) ParserError!*Node {
    const token = try p.expectToken(.keyword_continue);
    return p.ast.makeContinueStmt(token.loc);
}

fn parseIfStmt(p: *Parser) ParserError!*Node {
    const if_token = try p.expectToken(.keyword_if);

    if (p.peek().tag == .l_paren) {
        _ = p.advance();
    }
    const @"test" = try p.parseExpr(0);
    const consequent = try p.parseStmt();
    const alternate = if (p.peek().tag == .keyword_else) blk: {
        _ = p.advance();
        break :blk try p.parseStmt();
    } else null;
    const loc = if (alternate) |alt| if_token.loc.merge(&alt.loc) else if_token.loc.merge(&consequent.loc);
    return p.ast.makeIfStmt(loc, @"test", consequent, alternate);
}

fn parseBlockStmt(p: *Parser) ParserError!*Node {
    const l_brace = try p.expectToken(.l_brace);
    var list: std.ArrayList(*const Node) = .empty;
    while (p.peek().tag != .r_brace) {
        const stmt = try p.parseStmt();
        try list.append(p.ast.arena.allocator(), stmt);
    }

    const r_brace = try p.expectToken(.r_brace);
    return p.ast.makeBlockStmt(l_brace.loc.merge(&r_brace.loc), try list.toOwnedSlice(p.ast.arena.allocator()));
}

fn parseReturnStmt(p: *Parser) ParserError!*Node {
    const token = try p.expectToken(.keyword_return);
    //TODO: check for empty return statement
    const expr = try p.parseExpr(0);
    return p.ast.makeReturnStmt(token.loc.merge(&expr.loc), expr);
}

const OperInfo = struct {
    lbp: i8,
    rbp: i8,
    tag: Node.Tag,
};

const operTable = std.enums.directEnumArrayDefault(Token.Tag, OperInfo, .{ .lbp = -1, .rbp = -1, .tag = Node.Tag.root }, 0, .{
    .equal = .{ .lbp = 1, .rbp = 1, .tag = .assign },
    .plus_equal = .{ .lbp = 1, .rbp = 1, .tag = .add_assign },
    .minus_equal = .{ .lbp = 1, .rbp = 1, .tag = .sub_assign },
    .asterisk_equal = .{ .lbp = 1, .rbp = 1, .tag = .mul_assign },
    .slash_equal = .{ .lbp = 1, .rbp = 1, .tag = .div_assign },
    .modulus_equal = .{ .lbp = 1, .rbp = 1, .tag = .mod_assign },

    .pipe_pipe = .{ .lbp = 20, .rbp = 21, .tag = .@"or" },
    .ampersand_ampersand = .{ .lbp = 23, .rbp = 24, .tag = .@"and" },

    .equal_equal = .{ .lbp = 30, .rbp = 31, .tag = .equal_equal },
    .bang_equal = .{ .lbp = 30, .rbp = 31, .tag = .bang_equal },

    .angle_bracket_left = .{ .lbp = 40, .rbp = 41, .tag = .less },
    .angle_bracket_right = .{ .lbp = 40, .rbp = 41, .tag = .greater },
    .angle_bracket_left_equal = .{ .lbp = 40, .rbp = 41, .tag = .less_or_equal },
    .angle_bracket_right_equal = .{ .lbp = 40, .rbp = 41, .tag = .greater_or_equal },

    .plus = .{ .lbp = 60, .rbp = 61, .tag = .add },
    .minus = .{ .lbp = 60, .rbp = 61, .tag = .sub },
    .asterisk = .{ .lbp = 70, .rbp = 71, .tag = .mul },
    .slash = .{ .lbp = 70, .rbp = 71, .tag = .div },
    .modulus = .{ .lbp = 70, .rbp = 71, .tag = .mod },
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
            const value = try std.fmt.parseInt(i64, p.source[token.loc.start..token.loc.end], 10);
            return try p.ast.makeIntLiteral(token.loc, value);
        },
        .identifier => {
            p.advance();
            const name = p.source[token.loc.start..token.loc.end];
            // std.mem.trim(u8, name, "\t\r\n ")
            return try p.ast.makeIdentifier(token.loc, name);
        },
        .keyword_none => {
            p.advance();
            return try p.ast.makeNoneLiteral(token.loc);
        },
        .keyword_true => {
            p.advance();
            return try p.ast.makeBoolLiteral(token.loc, true);
        },
        .keyword_false => {
            p.advance();
            return try p.ast.makeBoolLiteral(token.loc, false);
        },
        else => {
            if (token.tag == .eof) {
                try p.recordError(token.loc, "Unexpected end of file", .{});
                return ParserError.UnexpectedEOF;
            }

            try p.recordError(token.loc, "Unexpected token '{s}'", .{@tagName(token.tag)});
            return ParserError.SyntaxError;
        },
    }
}
