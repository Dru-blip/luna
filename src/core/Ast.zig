const std = @import("std");
const Token = @import("Tokenizer.zig").Token;

const Ast = @This();

pub const Node = struct {
    tag: Tag,
    loc: Token.Loc,
    data: Data,

    const Tag = enum {
        root,
        expr_stmt,
        int_literal,
    };

    const Data = union {
        un: *Node,
        int: i64,
    };
};

pub const Nodes = std.ArrayList(Node);

nodes: Nodes,
arena: std.heap.ArenaAllocator,

pub fn init(gpa: std.mem.Allocator) Ast {
    return .{
        .arena = std.heap.ArenaAllocator.init(gpa),
        .nodes = .empty,
    };
}

pub fn make_node(ast: *Ast, tag: Node.Tag) !*Node {
    var node = try ast.arena.allocator().create(Node);
    node.tag = tag;
    return node;
}
