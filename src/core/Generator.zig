const std = @import("std");
const bytecode = @import("bytecode.zig");
const Ast = @import("Ast.zig");
const Span = @import("Tokenizer.zig").Token.Loc;
const Value = @import("Value.zig");
const Gc = @import("Gc.zig");
const String = @import("../runtime/String.zig");
const StringInterner = @import("../runtime/StringInterner.zig");

const Inst = bytecode.Inst;
const Constants = bytecode.Constants;
const Executable = bytecode.Executable;
const Instructions = bytecode.Instructions;

const Generator = @This();

pub const GenError = error{} || std.mem.Allocator.Error;

const Variables = std.ArrayList(Variable);
const Identifiers = std.ArrayList(*String);
const LoopStack = std.ArrayList(LoopInfo);

arena: std.heap.ArenaAllocator,
blocks: std.ArrayList(*BasicBlock),
gpa: std.mem.Allocator,
string_interner: *StringInterner,
current_block: *BasicBlock = undefined,
register_count: u32,
free_registers: std.ArrayList(u32),
ast: Ast,
gc: *Gc,
scope_depth: u32 = 0,
constants: Constants,
local_variables: Variables = .empty,
global_variables: Variables = .empty,
identifiers: Identifiers = .empty,
loop_stack: LoopStack = .empty,

const Scope = enum { global, local };

const Variable = struct {
    name: []const u8,
    scope_depth: u32,
    scope: Scope,
    allocated_reg_slot: u32,
};

const LoopInfo = struct {
    start_block: u32,
    end_block: u32,
};

pub const BasicBlock = struct {
    pub const Spans = std.ArrayList(Span);

    id: u32,
    instructions: Instructions,
    spans: Spans,
    start_offset: u32, //used when linearizing the blocks
};

pub fn init(gpa: std.mem.Allocator, ast: Ast, gc: *Gc, string_interner: *StringInterner) !Generator {
    var generator = Generator{
        .arena = std.heap.ArenaAllocator.init(gpa),
        .blocks = .empty,
        .constants = .empty,
        .gpa = gpa,
        .ast = ast,
        .register_count = 0,
        .free_registers = .empty,
        .gc = gc,
        .string_interner = string_interner,
    };

    const block = try generator.makeBasicBlock();
    generator.switchBasicBlock(block);

    return generator;
}

pub fn deinit(g: *Generator) void {
    g.constants.deinit(g.gpa);
    g.free_registers.deinit(g.gpa);
    g.global_variables.deinit(g.gpa);
    g.local_variables.deinit(g.gpa);
    g.identifiers.deinit(g.gpa);
    g.blocks.deinit(g.gpa);
    g.arena.deinit();
}

pub fn generate(g: *Generator) !*Executable {
    try g.genNodes(g.ast.nodes);
    try g.addInst(.hlt, .{ .none = {} }, .{ .start = 0, .col = 1, .end = 0, .line = 1 });
    return try g.finalize();
}

pub fn finalize(g: *Generator) !*Executable {
    var executable: *Executable = try Executable.new(g.gc);
    executable.constants = try g.constants.toOwnedSlice(g.gpa);

    executable.max_register_count = g.register_count;
    executable.global_variable_count = @intCast(g.global_variables.items.len);
    try g.linearizeBasicBlocks(executable);
    return executable;
}

pub fn makeBasicBlock(g: *Generator) !*BasicBlock {
    var block = try g.arena.allocator().create(BasicBlock);
    block.id = @intCast(g.blocks.items.len);
    block.instructions = .empty;
    block.spans = .empty;
    try g.blocks.append(g.gpa, block);

    return block;
}

fn switchBasicBlock(g: *Generator, block: *BasicBlock) void {
    g.current_block = block;
}

fn allocRegister(g: *Generator) u32 {
    if (g.free_registers.pop()) |reg| {
        return reg;
    }

    const reg = g.register_count;
    g.register_count += 1;
    return reg;
}

fn freeRegister(g: *Generator, reg: u32) !void {
    try g.free_registers.append(g.gpa, reg);
}

fn beginScope(g: *Generator) void {
    g.scope_depth += 1;
}

fn endScope(g: *Generator) void {
    var length = g.local_variables.items.len;
    while (length > 0) {
        const local = g.local_variables.items[length - 1];
        if (local.scope_depth == g.scope_depth) {
            _ = g.local_variables.pop();
            length -= 1;
        }
        break;
    }
    g.scope_depth -= 1;
}

fn declareVariable(g: *Generator, name: []const u8, variable: *Variable) !void {
    var i: usize = g.local_variables.items.len;
    while (i > 0) : (i -= 1) {
        const local = g.local_variables.items[i - 1];
        if (std.mem.eql(u8, local.name, name)) {
            variable.* = local;
            return;
        }
    }

    for (g.global_variables.items) |global| {
        if (std.mem.eql(u8, global.name, name)) {
            variable.* = global;
            return;
        }
    }

    const scope: Scope = if (g.scope_depth > 0) .local else .global;
    const slot: u32 = if (scope == .local) g.allocRegister() else @intCast(g.global_variables.items.len);
    variable.* = .{
        .name = name,
        .scope = scope,
        .scope_depth = g.scope_depth,
        .allocated_reg_slot = slot,
    };

    if (scope == .global) {
        try g.global_variables.append(g.gpa, variable.*);
    } else {
        try g.local_variables.append(g.gpa, variable.*);
    }
}

fn findVariable(g: *Generator, name: []const u8, result: *Variable) bool {
    var i: usize = g.local_variables.items.len;
    while (i > 0) : (i -= 1) {
        const local = g.local_variables.items[i - 1];
        if (std.mem.eql(u8, local.name, name)) {
            result.* = local;
            return true;
        }
    }

    for (g.global_variables.items) |global| {
        if (std.mem.eql(u8, global.name, name)) {
            result.* = global;
            return true;
        }
    }
    return false;
}

fn addConstant(g: *Generator, value: Value) !u32 {
    const index = g.constants.items.len;
    try g.constants.append(g.gpa, value);
    return @intCast(index);
}

fn addIdentifier(g: *Generator, name: []const u8) !u32 {
    const index = g.identifiers.items.len;
    const identifier = try g.string_interner.intern(name);
    try g.identifiers.append(g.gpa, identifier);
    return @intCast(index);
}

fn addInst(g: *Generator, op: Inst.Op, data: Inst.Data, span: Span) !void {
    try g.current_block.instructions.append(g.arena.allocator(), .{
        .op = op,
        .data = data,
    });

    try g.current_block.spans.append(g.arena.allocator(), span);
}

fn addUn(g: *Generator, op: Inst.Op, arg: u32, span: Span) !void {
    try g.addInst(op, .{ .un = arg }, span);
}

fn addBin(g: *Generator, op: Inst.Op, lhs: u32, rhs: u32, span: Span) !void {
    try g.addInst(op, .{ .bin = .{ .lhs = lhs, .rhs = rhs } }, span);
}

fn addTri(g: *Generator, op: Inst.Op, arg1: u32, arg2: u32, arg3: u32, span: Span) !void {
    try g.addInst(op, .{ .tri = .{ .op1 = arg1, .op2 = arg2, .dst = arg3 } }, span);
}

fn genNodes(g: *Generator, nodes: Ast.Nodes) GenError!void {
    for (nodes.items) |node| {
        try g.genStmt(node);
    }
}

fn genStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    switch (node.tag) {
        .let_decl => {
            try g.genLetDecl(node);
        },
        .while_stmt => {
            try g.genWhileStmt(node);
        },
        .loop_stmt => {
            try g.genLoopStmt(node);
        },
        .break_stmt => {
            try g.genBreakStmt(node);
        },
        .continue_stmt => {
            try g.genContinueStmt(node);
        },
        .if_stmt => {
            try g.genIfStmt(node);
        },
        .block => {
            try g.genBlockStmt(node);
        },
        .return_stmt => {
            const val = try g.genExpr(node.data.opt.?);
            try g.addUn(.ret, val, node.loc);
            try g.freeRegister(val);
        },
        .expr_stmt => {
            _ = try g.genExpr(node.data.un);
        },
        else => {
            unreachable;
        },
    }
}

fn genWhileStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    const test_block = try g.makeBasicBlock();
    const body_block = try g.makeBasicBlock();
    const end_block = try g.makeBasicBlock();
    const loop_info: LoopInfo = .{
        .start_block = test_block.id,
        .end_block = end_block.id,
    };
    try g.loop_stack.append(g.gpa, loop_info);
    g.switchBasicBlock(test_block);
    const cond = try g.genExpr(node.data.@"while".@"test");
    try g.addTri(.branch, cond, body_block.id, end_block.id, node.loc);
    g.switchBasicBlock(body_block);
    try g.genStmt(node.data.@"while".body);
    try g.addUn(.jmp, test_block.id, node.loc);

    g.switchBasicBlock(end_block);
}

fn genLoopStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    const start_block = try g.makeBasicBlock();
    const end_block = try g.makeBasicBlock();
    const loop_info: LoopInfo = .{
        .start_block = start_block.id,
        .end_block = end_block.id,
    };
    try g.loop_stack.append(g.gpa, loop_info);
    g.switchBasicBlock(start_block);
    try g.genStmt(node.data.un);
    try g.addUn(.jmp, start_block.id, node.loc);
    _ = g.loop_stack.pop();
    g.switchBasicBlock(end_block);
}

fn genBreakStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    const loop_info = g.loop_stack.getLast();
    try g.addUn(.jmp, loop_info.end_block, node.loc);
}

fn genContinueStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    const loop_info = g.loop_stack.getLast();
    try g.addUn(.jmp, loop_info.start_block, node.loc);
}

fn genIfStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    var current: ?*const Ast.Node = node;
    const end_block = try g.makeBasicBlock();

    while (current) |stmt| {
        if (stmt.tag != .if_stmt) break;
        const true_block = try g.makeBasicBlock();
        const false_block = try g.makeBasicBlock();
        const condition = try g.genExpr(stmt.data.@"if".@"test");
        try g.addTri(.branch, condition, true_block.id, false_block.id, stmt.loc);
        g.switchBasicBlock(true_block);
        try g.genStmt(stmt.data.@"if".consequent);
        try g.addUn(.jmp, end_block.id, stmt.loc);
        current = stmt.data.@"if".alternate;
        g.switchBasicBlock(false_block);
    }

    if (current) |stmt| {
        try g.genStmt(stmt);
        try g.addUn(.jmp, end_block.id, stmt.loc);
    } else {
        try g.addUn(.jmp, end_block.id, node.loc);
    }

    g.switchBasicBlock(end_block);
}

fn genLetDecl(g: *Generator, node: *const Ast.Node) GenError!void {
    var initializer: u32 = undefined;
    if (node.data.let.expr) |expr| {
        initializer = try g.genExpr(expr);
    } else {
        initializer = g.allocRegister();
        try g.addUn(.load_none, initializer, node.loc);
    }
    var variable: Variable = undefined;
    try g.declareVariable(node.data.let.name, &variable);

    try g.addBin(
        if (variable.scope == .global) .store_global_by_index else .mov,
        initializer,
        variable.allocated_reg_slot,
        node.loc,
    );
}

fn genBlockStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    g.beginScope();
    for (node.data.list) |stmt| {
        try g.genStmt(stmt);
    }
    g.endScope();
}

fn genExpr(g: *Generator, node: *const Ast.Node) GenError!u32 {
    switch (node.tag) {
        .int_literal => {
            const reg = g.allocRegister();
            const const_index = try g.addConstant(Value.int(node.data.int));
            try g.addBin(.load_const, const_index, reg, node.loc);
            return reg;
        },
        .bool_literal => {
            const reg = g.allocRegister();
            try g.addUn(if (node.data.bool) .load_true else .load_false, reg, node.loc);
            return reg;
        },
        .none_literal => {
            const reg = g.allocRegister();
            try g.addUn(.load_none, reg, node.loc);
            return reg;
        },
        .identifier => {
            var variable: Variable = undefined;
            var dst: u32 = undefined;
            if (g.findVariable(node.data.string, &variable)) {
                if (variable.scope == .local) {
                    return variable.allocated_reg_slot;
                }
                dst = g.allocRegister();
                try g.addBin(.load_global_by_index, variable.allocated_reg_slot, dst, node.loc);
                return dst;
            }

            dst = g.allocRegister();
            const identifier_index = try g.addIdentifier(node.data.string);
            try g.addBin(.load_global_by_index, identifier_index, dst, node.loc);
            return dst;
        },
        .add => {
            return try g.genBinOp(.add, node);
        },
        .sub => {
            return try g.genBinOp(.sub, node);
        },
        .mul => {
            return try g.genBinOp(.mul, node);
        },
        .div => {
            return try g.genBinOp(.div, node);
        },
        .mod => {
            return try g.genBinOp(.mod, node);
        },
        .less => {
            return try g.genBinOp(.test_lt, node);
        },
        .greater => {
            return try g.genBinOp(.test_gt, node);
        },
        .less_or_equal => {
            return try g.genBinOp(.test_le, node);
        },
        .greater_or_equal => {
            return try g.genBinOp(.test_ge, node);
        },
        .equal_equal => {
            return try g.genBinOp(.test_eq, node);
        },
        .bang_equal => {
            return try g.genBinOp(.test_neq, node);
        },
        .@"and" => {
            return try g.genLogicalOp(.@"and", node);
        },
        .@"or" => {
            return try g.genLogicalOp(.@"or", node);
        },
        .assign => {
            const value = try g.genExpr(node.data.bin.rhs);
            const name = node.data.bin.lhs.data.string;

            var variable: Variable = undefined;
            if (g.findVariable(name, &variable)) {
                try g.addBin(
                    if (variable.scope == .global) .store_global_by_index else .mov,
                    value,
                    variable.allocated_reg_slot,
                    node.loc,
                );
            } else {
                const identifier_index = try g.addIdentifier(name);
                try g.addBin(.store_global_by_name, value, identifier_index, node.loc);
            }

            return value;
        },
        else => {
            unreachable;
        },
    }
}

inline fn genBinOp(g: *Generator, op: Inst.Op, node: *const Ast.Node) GenError!u32 {
    const lhs = try g.genExpr(node.data.bin.lhs);
    const rhs = try g.genExpr(node.data.bin.rhs);

    const dst = g.allocRegister();
    try g.addTri(op, lhs, rhs, dst, node.loc);
    try g.freeRegister(lhs);
    try g.freeRegister(rhs);
    return dst;
}

inline fn genLogicalOp(g: *Generator, op: Ast.Node.Tag, node: *const Ast.Node) GenError!u32 {
    const lhs = try g.genExpr(node.data.bin.lhs);
    const dst = g.allocRegister();

    const rhs_block = try g.makeBasicBlock();
    const end_block = try g.makeBasicBlock();

    try g.addBin(.mov, lhs, dst, node.loc);

    var branch: Inst = .{
        .op = .branch,
        .data = .{
            .tri = .{
                .op1 = lhs,
                .op2 = rhs_block.id,
                .dst = end_block.id,
            },
        },
    };

    if (op == .@"or") {
        branch.data.tri.op2 = end_block.id;
        branch.data.tri.dst = rhs_block.id;
    }

    try g.current_block.instructions.append(g.arena.allocator(), branch);
    try g.current_block.spans.append(g.arena.allocator(), node.loc);

    g.switchBasicBlock(rhs_block);
    const rhs = try g.genExpr(node.data.bin.rhs);
    try g.addBin(.mov, rhs, dst, node.loc);

    try g.addUn(.jmp, end_block.id, node.loc);

    g.switchBasicBlock(end_block);
    return dst;
}

fn linearizeBasicBlocks(g: *Generator, executable: *Executable) !void {
    //TODO: find a better way to do this
    var block_start_offsets = try std.ArrayList(u32).initCapacity(g.gpa, g.blocks.items.len);
    defer block_start_offsets.deinit(g.gpa);

    var offset: u32 = 0;
    for (g.blocks.items) |block| {
        block.start_offset = offset;
        try block_start_offsets.append(g.gpa, offset);
        offset += @intCast(block.instructions.items.len);
    }

    const total_instructions = offset;
    var instructions = try std.ArrayList(Inst).initCapacity(g.gpa, total_instructions);

    var spans = try std.ArrayList(Span).initCapacity(g.gpa, total_instructions);

    for (g.blocks.items) |block| {
        try instructions.appendSlice(g.gpa, block.instructions.items);
        try spans.appendSlice(g.gpa, block.spans.items);
    }

    // TODO: find a better way to do this.
    // patch jump instructions
    //
    // instead of doing a full loop on the instructions
    // its better to store the jump instructions inside a seperate array (like a
    // patch buffer) and patch those to avoid unnecessary iterations.
    for (0..total_instructions) |i| {
        const instr = &instructions.items[i];
        switch (instr.op) {
            .jmp => {
                instr.data.un = block_start_offsets.items[instr.data.un];
            },
            .branch => {
                instr.data.tri.op2 = block_start_offsets.items[instr.data.tri.op2];
                instr.data.tri.dst = block_start_offsets.items[instr.data.tri.dst];
            },
            else => continue,
        }
    }
    executable.instructions = try instructions.toOwnedSlice(g.gpa);
    executable.spans = try spans.toOwnedSlice(g.gpa);
}
