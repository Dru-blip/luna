const std = @import("std");
const Ast = @import("Ast.zig");
const Value = @import("Value.zig");

const Eval = @This();

ast: Ast,

pub fn eval(ast: Ast) void {
    var intepreter: Eval = .{
        .ast = ast,
    };
    evalNodes(&intepreter, ast.nodes);
}

fn evalNodes(e: *Eval, nodes: Ast.Nodes) void {
    for (nodes.items) |node| {
        e.evalStmt(node);
    }
}

fn evalStmt(e: *Eval, node: *Ast.Node) void {
    switch (node.tag) {
        .return_stmt => {
            const val = e.evalExpr(node.data.opt.?);
            std.debug.print("{d}\n", .{val.data.int});
        },
        .expr_stmt => {
            const val = e.evalExpr(node.data.un);
            std.debug.print("{d}\n", .{val.data.int});
        },
        else => {
            unreachable;
        },
    }
}

fn evalExpr(e: *Eval, node: *Ast.Node) Value {
    switch (node.tag) {
        .int_literal => {
            return .{
                .type = .int,
                .data = .{
                    .int = node.data.int,
                },
            };
        },
        .add => {
            const lhs = e.evalExpr(node.data.bin.lhs);
            const rhs = e.evalExpr(node.data.bin.rhs);
            return .{
                .type = .int,
                .data = .{
                    .int = lhs.data.int + rhs.data.int,
                },
            };
        },
        .sub => {
            const lhs = e.evalExpr(node.data.bin.lhs);
            const rhs = e.evalExpr(node.data.bin.rhs);
            return .{
                .type = .int,
                .data = .{
                    .int = lhs.data.int - rhs.data.int,
                },
            };
        },
        .mul => {
            const lhs = e.evalExpr(node.data.bin.lhs);
            const rhs = e.evalExpr(node.data.bin.rhs);
            return .{
                .type = .int,
                .data = .{
                    .int = lhs.data.int * rhs.data.int,
                },
            };
        },
        .div => {
            const lhs = e.evalExpr(node.data.bin.lhs);
            const rhs = e.evalExpr(node.data.bin.rhs);
            return .{
                .type = .int,
                .data = .{
                    .int = @divFloor(lhs.data.int, rhs.data.int),
                },
            };
        },
        else => {
            unreachable;
        },
    }
}
