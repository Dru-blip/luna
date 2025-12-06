const std = @import("std");
const bytecode = @import("bytecode.zig");
const Ast = @import("Ast.zig");
const Span = @import("Tokenizer.zig").Token.Loc;
const Value = @import("Value.zig");
const Gc = @import("Gc.zig");

const Inst = bytecode.Inst;

const Constants = bytecode.Constants;
const Executable = bytecode.Executable;
const Instructions = bytecode.Instructions;
const Generator = @This();

arena: std.heap.ArenaAllocator,
blocks: std.ArrayList(*BasicBlock),
gpa: std.mem.Allocator,
current_block: *BasicBlock = undefined,
constants: Constants,
register_count: u32,
free_registers: std.ArrayList(u32),
ast: Ast,
gc: *Gc,

pub const BasicBlock = struct {
    pub const Spans = std.ArrayList(Span);

    id: u32,
    instructions: Instructions,
    spans: Spans,
    start_offset: u32, //used when linearizing the blocks
};

pub fn init(gpa: std.mem.Allocator, ast: Ast, gc: *Gc) !Generator {
    var generator = Generator{
        .arena = std.heap.ArenaAllocator.init(gpa),
        .blocks = .empty,
        .constants = .empty,
        .gpa = gpa,
        .ast = ast,
        .register_count = 0,
        .free_registers = .empty,
        .gc = gc,
    };

    const block = try generator.makeBasicBlock();
    generator.switchBasicBlock(block);

    return generator;
}

pub fn deinit(g: *Generator) void {
    g.constants.deinit(g.gpa);
    g.free_registers.deinit(g.gpa);
    g.blocks.deinit(g.gpa);
    g.arena.deinit();
}

pub fn generate(g: *Generator) !*Executable {
    try g.genNodes(g.ast.nodes);
    return try g.finalize();
}

pub fn finalize(g: *Generator) !*Executable {
    var executable: *Executable = try g.gc.alloc(Executable);
    executable.constants = try g.constants.toOwnedSlice(g.gpa);

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

fn addConstant(g: *Generator, value: Value) !u32 {
    const index = g.constants.items.len;
    try g.constants.append(g.gpa, value);
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
    try g.addInst(op, .{ .tri = .{ .arg1 = arg1, .arg2 = arg2, .arg3 = arg3 } }, span);
}

fn genNodes(g: *Generator, nodes: Ast.Nodes) !void {
    for (nodes.items) |node| {
        try g.genStmt(node);
    }
}

fn genStmt(g: *Generator, node: *Ast.Node) !void {
    switch (node.tag) {
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

fn genExpr(g: *Generator, node: *Ast.Node) !u32 {
    switch (node.tag) {
        .int_literal => {
            const reg = g.allocRegister();
            const const_index = try g.addConstant(Value.fromInt(node.data.int));
            try g.addBin(.load_const, const_index, reg, node.loc);
            return reg;
        },
        .add => {
            const lhs = try g.genExpr(node.data.bin.lhs);
            const rhs = try g.genExpr(node.data.bin.rhs);
            const dst = g.allocRegister();
            try g.addTri(.add, lhs, rhs, dst, node.loc);
            try g.freeRegister(lhs);
            try g.freeRegister(rhs);
            return dst;
        },
        .sub => {
            const lhs = try g.genExpr(node.data.bin.lhs);
            const rhs = try g.genExpr(node.data.bin.rhs);

            const dst = g.allocRegister();
            try g.addTri(.sub, lhs, rhs, dst, node.loc);
            try g.freeRegister(lhs);
            try g.freeRegister(rhs);
            return dst;
        },
        .mul => {
            const lhs = try g.genExpr(node.data.bin.lhs);
            const rhs = try g.genExpr(node.data.bin.rhs);

            const dst = g.allocRegister();
            try g.addTri(.mul, lhs, rhs, dst, node.loc);
            try g.freeRegister(lhs);
            try g.freeRegister(rhs);
            return dst;
        },
        .div => {
            const lhs = try g.genExpr(node.data.bin.lhs);
            const rhs = try g.genExpr(node.data.bin.rhs);

            const dst = g.allocRegister();
            try g.addTri(.div, lhs, rhs, dst, node.loc);
            try g.freeRegister(lhs);
            try g.freeRegister(rhs);
            return dst;
        },
        else => {
            unreachable;
        },
    }
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

    // for (size_t i = 0; i < total_instructions; i++) {
    //     struct instruction* instr = &flat[i];
    //     switch (instr->opcode) {
    //         case OPCODE_JUMP: {
    //             instr->jmp.target_offset = block_start_offsets[instr->jmp.target_offset];
    //             break;
    //         }
    //         case OPCODE_JMP_IF: {
    //             instr->jmp_if.true_block_id = block_start_offsets[instr->jmp_if.true_block_id];
    //             instr->jmp_if.false_block_id = block_start_offsets[instr->jmp_if.false_block_id];
    //             break;
    //         }
    //         case OPCODE_ITER_NEXT: {
    //             instr->iter_next.jmp_offset = block_start_offsets[instr->iter_next.jmp_offset];
    //             break;
    //         }
    //         default:
    //             break;
    //     }
    // }
    //
    executable.instructions = try instructions.toOwnedSlice(g.gpa);
    executable.spans = try spans.toOwnedSlice(g.gpa);
}
