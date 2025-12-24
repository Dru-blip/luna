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
        if_stmt,
        loop_stmt,
        while_stmt,
        for_stmt,
        break_stmt,
        continue_stmt,
        return_stmt,
        expr_stmt,
        let_decl,
        function_decl,

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

        call,

        object_expr,
        object_property,
        member_expr,
        computed_member_expr,

        this_expr,
        function_expr,

        identifier,
        int_literal,
        float_literal,
        num_literal,
        bool_literal,
        none_literal,
        string_literal,
    };

    const Data = union {
        un: *Node,
        bin: struct {
            lhs: *Node,
            rhs: *Node,
        },
        list: []*const Node,
        opt: ?*Node,
        int: i64,
        float: f64,
        bool: bool,
        none: void,
        string: []const u8,
        call: struct {
            callee: *Node,
            args: []*Node,
        },
        property: struct {
            key: *Node,
            value: *Node,
        },
        member: struct {
            object: *Node,
            property: []const u8,
        },
        @"if": struct {
            @"test": *Node,
            consequent: *Node,
            alternate: ?*Node,
        },
        @"while": struct {
            @"test": *Node,
            body: *Node,
        },
        @"for": struct {
            init: *Node,
            @"test": *Node,
            update: *Node,
            body: *Node,
        },
        let: struct {
            name: []const u8,
            expr: ?*Node,
        },
        fndecl: struct {
            name: []const u8,
            params: [][]const u8,
            body: *Node,
        },
    };
};

pub const Nodes = std.ArrayList(*Node);

nodes: Nodes,
arena: std.heap.ArenaAllocator,
gpa: std.mem.Allocator,
source: [:0]const u8,
filepath: []const u8,

pub fn init(gpa: std.mem.Allocator, source: [:0]const u8, filepath: []const u8) Ast {
    return .{
        .arena = std.heap.ArenaAllocator.init(gpa),
        .nodes = .empty,
        .gpa = gpa,
        .source = source,
        .filepath = filepath,
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

pub fn parse(filepath: []const u8, source: [:0]const u8, gpa: std.mem.Allocator) !Ast {
    const tokens = try Tokenizer.tokenize(source, gpa);
    var parser = Parser.init(gpa, source, tokens, filepath);

    return try parser.parse();
}

pub fn makeNode(ast: *Ast, tag: Node.Tag, loc: Token.Loc) !*Node {
    const node = try ast.arena.allocator().create(Node);
    node.* = .{
        .tag = tag,
        .loc = loc,
        .data = undefined,
    };
    return node;
}

pub fn makeExprStmt(ast: *Ast, expr: *Node) !*Node {
    var node = try makeNode(ast, .expr_stmt, expr.loc);
    node.data = .{ .un = expr };
    return node;
}

pub fn makeIntLiteral(ast: *Ast, loc: Token.Loc, value: i64) !*Node {
    var node = try makeNode(ast, .int_literal, loc);
    node.data = .{ .int = value };
    return node;
}

pub fn makeFloatLiteral(ast: *Ast, loc: Token.Loc, value: f64) !*Node {
    var node = try makeNode(ast, .num_literal, loc);
    node.data = .{ .float = value };
    return node;
}

pub fn makeStringLiteral(ast: *Ast, loc: Token.Loc, value: []const u8) !*Node {
    var node = try makeNode(ast, .string_literal, loc);
    node.data = .{ .string = value };
    return node;
}

pub fn makeIdentifier(ast: *Ast, loc: Token.Loc, name: []const u8) !*Node {
    var node = try makeNode(ast, .identifier, loc);
    node.data = .{ .string = name };
    return node;
}

pub fn makeBoolLiteral(ast: *Ast, loc: Token.Loc, value: bool) !*Node {
    var node = try makeNode(ast, .bool_literal, loc);
    node.data = .{ .bool = value };
    return node;
}

pub fn makeNoneLiteral(ast: *Ast, loc: Token.Loc) !*Node {
    var node = try makeNode(ast, .none_literal, loc);
    node.data = .{ .none = {} };
    return node;
}

pub fn makeBinOp(ast: *Ast, tag: Node.Tag, loc: Token.Loc, lhs: *Node, rhs: *Node) !*Node {
    var node = try makeNode(ast, tag, loc);
    node.data = .{ .bin = .{ .lhs = lhs, .rhs = rhs } };
    return node;
}

pub fn makeReturnStmt(ast: *Ast, loc: Token.Loc, expr: *Node) !*Node {
    var node = try makeNode(ast, .return_stmt, loc);
    node.data = .{ .opt = expr };
    return node;
}

pub fn makeLetDecl(ast: *Ast, loc: Token.Loc, name: []const u8, expr: ?*Node) !*Node {
    var node = try makeNode(ast, .let_decl, loc);
    node.data = .{ .let = .{ .name = name, .expr = expr } };
    return node;
}

pub fn makeBlockStmt(ast: *Ast, loc: Token.Loc, list: []*const Node) !*Node {
    var node = try makeNode(ast, .block, loc);
    node.data = .{ .list = list };
    return node;
}

pub fn makeIfStmt(ast: *Ast, loc: Token.Loc, @"test": *Node, consequent: *Node, alternate: ?*Node) !*Node {
    var node = try makeNode(ast, .if_stmt, loc);
    node.data = .{
        .@"if" = .{
            .@"test" = @"test",
            .consequent = consequent,
            .alternate = alternate,
        },
    };
    return node;
}

pub fn makeLoopStmt(ast: *Ast, loc: Token.Loc, body: *Node) !*Node {
    var node = try makeNode(ast, .loop_stmt, loc);
    node.data = .{ .un = body };
    return node;
}

pub fn makeBreakStmt(ast: *Ast, loc: Token.Loc) !*Node {
    var node = try makeNode(ast, .break_stmt, loc);
    node.data = .{ .none = {} };
    return node;
}

pub fn makeContinueStmt(ast: *Ast, loc: Token.Loc) !*Node {
    var node = try makeNode(ast, .continue_stmt, loc);
    node.data = .{ .none = {} };
    return node;
}

pub fn makeWhileStmt(ast: *Ast, loc: Token.Loc, @"test": *Node, body: *Node) !*Node {
    var node = try makeNode(ast, .while_stmt, loc);
    node.data = .{
        .@"while" = .{
            .@"test" = @"test",
            .body = body,
        },
    };
    return node;
}

pub fn makeForStmt(ast: *Ast, loc: Token.Loc, initializer: *Node, @"test": *Node, update: *Node, body: *Node) !*Node {
    var node = try makeNode(ast, .for_stmt, loc);
    node.data = .{
        .@"for" = .{
            .init = initializer,
            .@"test" = @"test",
            .update = update,
            .body = body,
        },
    };
    return node;
}

pub fn makeFunctionDecl(ast: *Ast, loc: Token.Loc, name: []const u8, params: [][]const u8, body: *Node) !*Node {
    var node = try makeNode(ast, .function_decl, loc);
    node.data = .{
        .fndecl = .{
            .name = name,
            .params = params,
            .body = body,
        },
    };
    return node;
}

pub fn makeCall(ast: *Ast, loc: Token.Loc, callee: *Node, args: []*Node) !*Node {
    var node = try makeNode(ast, .call, loc);
    node.data = .{
        .call = .{
            .callee = callee,
            .args = args,
        },
    };
    return node;
}

pub fn makeProperty(ast: *Ast, loc: Token.Loc, key: *Node, value: *Node) !*Node {
    var node = try makeNode(ast, .object_property, loc);
    node.data = .{
        .property = .{
            .key = key,
            .value = value,
        },
    };
    return node;
}

pub fn makeObjectExpr(ast: *Ast, loc: Token.Loc, properties: []*const Node) !*Node {
    var node = try makeNode(ast, .object_expr, loc);
    node.data = .{
        .list = properties,
    };
    return node;
}

pub fn makeFunctionExpr(ast: *Ast, loc: Token.Loc, params: [][]const u8, body: *Node) !*Node {
    var node = try makeNode(ast, .function_expr, loc);
    node.data = .{
        .fndecl = .{
            .name = undefined,
            .params = params,
            .body = body,
        },
    };
    return node;
}
