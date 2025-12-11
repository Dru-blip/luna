const std = @import("std");
const Tokenizer = @import("Tokenizer.zig");
const Parser = @import("Parser.zig");

const Token = Tokenizer.Token;

const Ast = @This();

pub const Node = struct {
    tag: Tag,
    loc: Token.Loc,
    data: Data,

    pub const Tag = enum {
        root,
        block,
        return_stmt,
        expr_stmt,

        add,
        sub,
        mul,
        div,
        mod,

        less,
        greater,
        less_or_equal,
        greater_or_equal,
        equal_equal,
        bang_equal,

        @"and",
        @"or",

        assign,
        add_assign,
        sub_assign,
        mul_assign,
        div_assign,
        mod_assign,

        int_literal,
        bool_literal,
        none_literal,
    };

    const Data = union {
        un: *Node,
        bin: struct {
            lhs: *Node,
            rhs: *Node,
        },
        list: []*Node,
        opt: ?*Node,
        int: i64,
        bool: bool,
        none: void,
    };
};

pub const Nodes = std.ArrayList(*Node);

nodes: Nodes,
arena: std.heap.ArenaAllocator,
gpa: std.mem.Allocator,
source: [:0]const u8,

pub fn init(gpa: std.mem.Allocator, source: [:0]const u8) Ast {
    return .{
        .arena = std.heap.ArenaAllocator.init(gpa),
        .nodes = .empty,
        .gpa = gpa,
        .source = source,
    };
}

pub fn deinit(ast: *Ast) void {
    for (ast.nodes.items) |node| {
        switch (node.tag) {
            else => {},
        }
    }

    ast.arena.deinit();
    ast.nodes.deinit(ast.gpa);
}

pub fn parse(source: [:0]const u8, gpa: std.mem.Allocator) !Ast {
    const tokens = try Tokenizer.tokenize(source, gpa);
    var parser = Parser.init(gpa, source, tokens);

    return try parser.parse();
}

pub fn makeNode(ast: *Ast, tag: Node.Tag) !*Node {
    var node = try ast.arena.allocator().create(Node);
    node.tag = tag;
    return node;
}

pub fn makeExprStmt(ast: *Ast, expr: *Node) !*Node {
    var node = try ast.arena.allocator().create(Node);
    node.tag = .expr_stmt;
    node.data = .{ .un = expr };
    return node;
}

pub fn makeIntLiteral(ast: *Ast, loc: Token.Loc, value: i64) !*Node {
    var node = try ast.arena.allocator().create(Node);
    node.tag = .int_literal;
    node.loc = loc;
    node.data = .{ .int = value };
    return node;
}

pub fn makeBoolLiteral(ast: *Ast, loc: Token.Loc, value: bool) !*Node {
    var node = try ast.arena.allocator().create(Node);
    node.tag = .bool_literal;
    node.loc = loc;
    node.data = .{ .bool = value };
    return node;
}

pub fn makeNoneLiteral(ast: *Ast, loc: Token.Loc) !*Node {
    var node = try ast.arena.allocator().create(Node);
    node.tag = .none_literal;
    node.loc = loc;
    node.data = .{ .none = {} };
    return node;
}

pub fn makeBinOp(ast: *Ast, tag: Node.Tag, loc: Token.Loc, lhs: *Node, rhs: *Node) !*Node {
    var node = try ast.arena.allocator().create(Node);
    node.tag = tag;
    node.loc = loc;
    node.data = .{ .bin = .{ .lhs = lhs, .rhs = rhs } };
    return node;
}

pub fn makeReturnStmt(ast: *Ast, loc: Token.Loc, expr: *Node) !*Node {
    var node = try ast.arena.allocator().create(Node);
    node.tag = .return_stmt;
    node.loc = loc;
    node.data = .{ .opt = expr };
    return node;
}
