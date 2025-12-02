const std = @import("std");
const Token = @import("Tokenizer.zig").Token;

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
        int_literal,
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
    };
};

pub const Nodes = std.ArrayList(*Node);

nodes: Nodes,
arena: std.heap.ArenaAllocator,
gpa: std.mem.Allocator,

pub fn init(gpa: std.mem.Allocator) Ast {
    return .{
        .arena = std.heap.ArenaAllocator.init(gpa),
        .nodes = .empty,
        .gpa = gpa,
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
