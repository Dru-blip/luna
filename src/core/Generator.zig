//TODO: should implement a fixed register pool allocation
const std = @import("std");
const bytecode = @import("bytecode.zig");
const Ast = @import("Ast.zig");
const Span = @import("Tokenizer.zig").Token.Loc;
const Value = @import("Value.zig");
const Gc = @import("Gc.zig");
const Exception = @import("../runtime/Exception.zig");
const String = @import("../runtime/String.zig");
const Object = @import("../runtime/Object.zig");
const StringInterner = @import("../runtime/StringInterner.zig");

const Inst = bytecode.Inst;
const Constants = bytecode.Constants;
const Executable = bytecode.Executable;
const Instructions = bytecode.Instructions;
const ExceptionHandlerBlock = bytecode.ExceptionHandlerBlock;
const RescueHandler = bytecode.RescueHandler;

const Generator = @This();

pub const GenError = error{} || std.mem.Allocator.Error;

const Variables = std.ArrayList(Variable);
const Identifiers = std.ArrayList(Value);
const LoopStack = std.ArrayList(LoopInfo);

arena: std.heap.ArenaAllocator,
blocks: std.ArrayList(*BasicBlock),
gpa: std.mem.Allocator,
string_interner: *StringInterner,
current_block: *BasicBlock = undefined,
register_count: u32 = 1,
free_registers: std.ArrayList(u32),
exception_handlers: std.ArrayList(ExceptionHandlerBlock) = .empty,
ast: Ast,
gc: *Gc,
scope_depth: u32 = 0,
constants: Constants,
local_variables: Variables = .empty,
global_variables: Variables = .empty,
identifiers: Identifiers = .empty,
loop_stack: LoopStack = .empty,
enclosing: ?*Generator = null,
extra: std.ArrayList(u32) = .empty,
arg_stack: std.ArrayList(u32) = .empty,

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

const ArgFrame = struct {
    base: usize,
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
    if (g.enclosing == null) {
        g.global_variables.deinit(g.gpa);
    }
    g.local_variables.deinit(g.gpa);
    g.identifiers.deinit(g.gpa);
    g.blocks.deinit(g.gpa);
    g.arg_stack.deinit(g.gpa);
    g.arena.deinit();
}

pub fn generate(g: *Generator) !*Executable {
    try g.genNodes(g.ast.nodes);
    try g.addInst(.hlt, .{ .none = {} }, .{ .start = 0, .col = 1, .end = 0, .line = 1 });
    var exe = try g.finalize();
    exe.param_count = 0;
    exe.name = g.gc.interpreter.common_names.module;
    return exe;
}

pub fn finalize(g: *Generator) !*Executable {
    var executable: *Executable = try Executable.new(g.gc);
    executable.constants = try g.constants.toOwnedSlice(g.gpa);
    executable.identifiers = try g.identifiers.toOwnedSlice(g.gpa);
    executable.filepath = g.ast.filepath;
    executable.max_register_count = g.register_count;
    executable.global_variable_count = @intCast(g.global_variables.items.len);
    executable.exception_handlers = try g.exception_handlers.toOwnedSlice(g.gpa);
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
    // if (g.free_registers.pop()) |reg| {
    //     return reg;
    // }

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

pub fn beginArgFrame(g: *Generator) !ArgFrame {
    return ArgFrame{ .base = g.arg_stack.items.len };
}

pub fn pushArg(g: *Generator, arg: u32) !void {
    try g.arg_stack.append(g.gpa, arg);
}

pub fn endArgFrame(g: *Generator, frame: *ArgFrame) void {
    g.arg_stack.shrinkRetainingCapacity(frame.base);
}

fn commitArgs(
    g: *Generator,
    frame: ArgFrame,
) !struct { offset: u32, argc: u32 } {
    const start = frame.base;
    const count = g.arg_stack.items.len - start;

    const offset = g.extra.items.len;
    try g.extra.appendSlice(g.gpa, g.arg_stack.items[start..]);

    return .{
        .offset = @intCast(offset),
        .argc = @intCast(count),
    };
}

fn declareVariable(g: *Generator, name: []const u8, out: *Variable) !void {
    var i: usize = g.local_variables.items.len;
    while (i > 0) : (i -= 1) {
        const local = g.local_variables.items[i - 1];
        if (std.mem.eql(u8, local.name, name) and local.scope_depth == g.scope_depth) {
            out.* = local;
            return;
        }
    }

    for (g.global_variables.items) |global| {
        if (std.mem.eql(u8, global.name, name)) {
            out.* = global;
            return;
        }
    }

    const scope: Scope = if (g.scope_depth > 0) .local else .global;
    const slot: u32 = if (scope == .local) g.allocRegister() else @intCast(g.global_variables.items.len);
    out.* = .{
        .name = name,
        .scope = scope,
        .scope_depth = g.scope_depth,
        .allocated_reg_slot = slot,
    };

    if (scope == .global) {
        try g.global_variables.append(g.gpa, out.*);
    } else {
        try g.local_variables.append(g.gpa, out.*);
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
    try g.identifiers.append(g.gpa, Value.object(Object.from(identifier)));
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
        .guard_stmt => {
            try g.genGuardStmt(node);
        },
        .class_decl => {
            try g.genClassDecl(node);
        },
        .function_decl => {
            try g.genFuncDecl(node);
        },
        .let_decl => {
            try g.genLetDecl(node);
        },
        .for_stmt => {
            try g.genForStmt(node);
        },
        .foreach_stmt => {
            try g.genForEachStmt(node);
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
            // try g.freeRegister(val);
        },
        .raise_stmt => {
            const exception = try g.genExpr(node.data.opt.?);
            try g.addUn(.raise_exception, exception, node.loc);
            // try g.freeRegister(exception);
        },
        .expr_stmt => {
            _ = try g.genExpr(node.data.un);
        },
        else => {
            unreachable;
        },
    }
}

fn genGuardStmt(g: *Generator, node: *const Ast.Node) !void {
    const guard_block = try g.makeBasicBlock();
    const end_block = try g.makeBasicBlock();
    var exeception_handler: ExceptionHandlerBlock = .{
        .start_offset = guard_block.id,
        .end_offset = guard_block.id,
        .rescues = &.{},
    };

    var rescue_handlers: std.ArrayList(RescueHandler) = .empty;
    try g.addUn(.jmp, guard_block.id, node.data.guard_stmt.body.loc);

    g.switchBasicBlock(guard_block);
    try g.genBlockStmt(node.data.guard_stmt.body);
    exeception_handler.end_offset = @intCast(guard_block.instructions.items.len + 1);

    var ensure_block_id: ?u32 = null;
    if (node.data.guard_stmt.ensure) |ensure| {
        const ensure_block = try g.makeBasicBlock();
        ensure_block_id = ensure_block.id;
        try g.addUn(.jmp, ensure_block.id, ensure.loc);
        exeception_handler.ensure_offset = ensure_block.id;
        g.switchBasicBlock(ensure_block);
        try g.genBlockStmt(ensure);
        try g.addUn(.jmp, end_block.id, ensure.loc);
    }

    for (node.data.guard_stmt.handlers) |handler| {
        const handler_block = try g.makeBasicBlock();

        var exception_class_var: Variable = undefined;
        var execetion_type = try g.addIdentifier(handler.class);
        var class_loc: u32 = 0;

        if (g.findVariable(handler.class, &exception_class_var)) {
            execetion_type = exception_class_var.allocated_reg_slot;
            class_loc = if (exception_class_var.scope == .global) 1 else 0;
        } else {
            class_loc = 2;
        }

        var exeception_reg: ?u32 = null;

        var exeception_variable: Variable = undefined;
        if (handler.binding) |exeception_variable_name| {
            try g.declareVariable(exeception_variable_name, &exeception_variable);
            exeception_reg = exeception_variable.allocated_reg_slot;
        }

        var rescue_handler: RescueHandler = .{
            .exception_type = execetion_type,
            .handler_offset = handler_block.id,
            .exception_register = exeception_reg,
            .exeception_class_loc = class_loc,
        };

        try g.addUn(.jmp, handler_block.id, handler.block.loc);
        g.switchBasicBlock(handler_block);
        try g.genBlockStmt(handler.block);

        if (ensure_block_id) |enusre_offset| {
            rescue_handler.ensure_offset = enusre_offset;
            try g.addUn(.jmp, enusre_offset, handler.block.loc);
        } else {
            try g.addUn(.jmp, end_block.id, handler.block.loc);
        }

        try rescue_handlers.append(g.gpa, rescue_handler);
    }

    if (node.data.guard_stmt.alternate) |alternate| {
        const alternate_block = try g.makeBasicBlock();
        try g.addUn(.jmp, alternate_block.id, alternate.loc);
        exeception_handler.else_offset = alternate_block.id;
        g.switchBasicBlock(alternate_block);
        try g.genBlockStmt(alternate);
    }

    exeception_handler.rescues = try rescue_handlers.toOwnedSlice(g.gpa);
    try g.exception_handlers.append(g.gpa, exeception_handler);

    g.switchBasicBlock(end_block);
}

fn genClassDecl(g: *Generator, node: *const Ast.Node) !void {
    var variable: Variable = undefined;
    try g.declareVariable(node.data.class_decl.name, &variable);

    const class_name_index = try g.addIdentifier(node.data.class_decl.name);
    const class_reg = g.allocRegister();
    try g.addBin(.build_class, class_name_index, class_reg, node.loc);
    try g.addBin(if (variable.scope == .global) .store_global_by_index else .mov, class_reg, variable.allocated_reg_slot, node.loc);

    if (node.data.class_decl.super_class) |super_class| {
        var super_class_var: Variable = undefined;
        var super_class_reg: u32 = undefined;
        if (g.findVariable(super_class, &super_class_var)) {
            if (variable.scope == .global) {
                super_class_reg = g.allocRegister();
                try g.addBin(.load_global_by_index, super_class_var.allocated_reg_slot, super_class_reg, node.loc);
            } else {
                super_class_reg = super_class_var.allocated_reg_slot;
            }
        } else {
            const super_class_name_index = try g.addIdentifier(super_class);
            super_class_reg = g.allocRegister();
            try g.addBin(.load_global_by_name, super_class_name_index, super_class_reg, node.loc);
        }

        try g.addBin(.set_super_class, super_class_reg, class_reg, node.loc);
    }

    for (node.data.class_decl.methods) |method| {
        const method_reg = try g.genFunctionExpr(method, method.data.fndecl.name);
        try g.addBin(.add_class_method, method_reg, class_reg, method.loc);
    }
}

fn declareParam(g: *Generator, name: []const u8) !void {
    const va = Variable{
        .scope = .local,
        .scope_depth = g.scope_depth,
        .name = name,
        .allocated_reg_slot = g.allocRegister(),
    };
    try g.local_variables.append(g.gpa, va);
}

fn genFuncDecl(g: *Generator, node: *const Ast.Node) GenError!void {
    const name = try g.string_interner.intern(node.data.fndecl.name);
    var variable: Variable = undefined;
    try g.declareVariable(node.data.fndecl.name, &variable);

    var func_gen = try Generator.init(g.gpa, g.ast, g.gc, g.string_interner);
    defer func_gen.deinit();
    func_gen.enclosing = g;
    func_gen.gc.interpreter.generator = &func_gen;
    func_gen.global_variables = g.global_variables;
    func_gen.scope_depth = 1;
    for (node.data.fndecl.params) |param| {
        try func_gen.declareParam(param);
    }
    try func_gen.genStmt(node.data.fndecl.body);

    //TODO: we dont need a register for the return value but we allocated it anyways,
    // have to remove it.
    try func_gen.addUn(.ret_none, func_gen.allocRegister(), node.loc);
    var executable = try func_gen.finalize();
    executable.name = name;
    executable.param_count = @intCast(node.data.fndecl.params.len);

    const executable_index = try g.addConstant(Value.object(Object.from(executable)));
    const function_index = g.allocRegister();
    try g.addBin(.build_function, executable_index, function_index, node.loc);
    try g.addBin(if (variable.scope == .global) .store_global_by_index else .mov, function_index, variable.allocated_reg_slot, node.loc);
    func_gen.gc.interpreter.generator = func_gen.enclosing;
}

fn genForEachStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    const init_block = try g.makeBasicBlock();
    const head_block = try g.makeBasicBlock();
    const body_block = try g.makeBasicBlock();
    const end_block = try g.makeBasicBlock();

    const loop_info: LoopInfo = .{
        .start_block = head_block.id,
        .end_block = end_block.id,
    };

    try g.loop_stack.append(g.gpa, loop_info);
    try g.addUn(.jmp, init_block.id, node.loc);

    g.beginScope();

    var loop_variable: Variable = undefined;
    try g.declareVariable(node.data.foreach.variable, &loop_variable);

    g.switchBasicBlock(init_block);
    const iterable_reg = try g.genExpr(node.data.foreach.iterable);
    const iterator_reg = g.allocRegister();
    try g.addBin(.get_iter, iterable_reg, iterator_reg, node.data.foreach.iterable.loc);
    try g.addUn(.jmp, head_block.id, node.loc);

    g.switchBasicBlock(head_block);
    const next_value_reg = g.allocRegister();
    try g.addBin(.iter_next, iterator_reg, next_value_reg, node.data.foreach.iterable.loc);
    // TODO: Avoid relying on a falsy check here.
    // If the iterator returns a falsy value, the iteration stops prematurely.
    try g.addTri(.branch, next_value_reg, body_block.id, end_block.id, node.loc);

    g.switchBasicBlock(body_block);
    try g.addBin(.mov, next_value_reg, loop_variable.allocated_reg_slot, node.loc);

    try g.genStmt(node.data.foreach.body);
    try g.addUn(.jmp, head_block.id, node.loc);

    _ = g.loop_stack.pop();
    g.switchBasicBlock(end_block);
    g.endScope();
}

fn genForStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    const init_block = try g.makeBasicBlock();
    const test_block = try g.makeBasicBlock();
    const body_block = try g.makeBasicBlock();
    const update_block = try g.makeBasicBlock();
    const end_block = try g.makeBasicBlock();

    const loop_info: LoopInfo = .{
        .start_block = update_block.id,
        .end_block = end_block.id,
    };
    try g.loop_stack.append(g.gpa, loop_info);
    try g.addUn(.jmp, init_block.id, node.loc);

    g.beginScope();

    g.switchBasicBlock(init_block);
    //TODO: raise error if init is not a let decl.
    try g.genStmt(node.data.@"for".init);
    try g.addUn(.jmp, test_block.id, node.data.@"for".init.loc);

    g.switchBasicBlock(test_block);
    try g.addTri(.branch, try g.genExpr(node.data.@"for".@"test"), body_block.id, end_block.id, node.data.@"for".@"test".loc);

    g.switchBasicBlock(body_block);
    try g.genStmt(node.data.@"for".body);
    try g.addUn(.jmp, update_block.id, node.loc);

    g.switchBasicBlock(update_block);
    _ = try g.genExpr(node.data.@"for".update);
    try g.addUn(.jmp, test_block.id, node.loc);

    g.endScope();
    _ = g.loop_stack.pop();
    g.switchBasicBlock(end_block);
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
    //TODO: raise error if outside of loop
    const loop_info = g.loop_stack.getLast();
    try g.addUn(.jmp, loop_info.end_block, node.loc);
}

fn genContinueStmt(g: *Generator, node: *const Ast.Node) GenError!void {
    //TODO: raise error if outside of loop
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
        .this_expr => {
            return 0;
        },
        .num_literal => {
            const reg = g.allocRegister();
            const const_index = try g.addConstant(Value.number(node.data.float));
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
        .undefined_literal => {
            const reg = g.allocRegister();
            try g.addUn(.load_undefined, reg, node.loc);
            return reg;
        },
        .string_literal => {
            const reg = g.allocRegister();
            const const_index = try g.addConstant(Value.object(try String.new(g.gc, node.data.string)));
            try g.addBin(.load_const, const_index, reg, node.loc);
            return reg;
        },
        .identifier => {
            return try g.genIdentifier(node);
        },
        .un_negate => {
            const reg = g.allocRegister();
            const arg = try g.genExpr(node.data.un);
            try g.addBin(.negate, arg, reg, node.loc);
            return reg;
        },
        .not => {
            const reg = g.allocRegister();
            const arg = try g.genExpr(node.data.un);
            try g.addBin(.not, arg, reg, node.loc);
            return reg;
        },
        .un_plus => {
            const reg = g.allocRegister();
            const arg = try g.genExpr(node.data.un);
            try g.addBin(.un_plus, arg, reg, node.loc);
            return reg;
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
        .bitwise_and => {
            return try g.genBinOp(.@"and", node);
        },
        .bitwise_or => {
            return try g.genBinOp(.@"or", node);
        },
        .bitwise_xor => {
            return try g.genBinOp(.xor, node);
        },
        .@"and" => {
            return try g.genLogicalOp(.@"and", node);
        },
        .@"or" => {
            return try g.genLogicalOp(.@"or", node);
        },
        .super_expr => {
            const reg = g.allocRegister();
            try g.addUn(.get_super_class, reg, node.loc);
            return reg;
        },
        .assign => {
            return try g.genAssign(node, .assign, .add);
        },
        .add_assign => {
            return try g.genAssign(node, .add_assign, .add);
        },
        .sub_assign => {
            return try g.genAssign(node, .sub_assign, .sub);
        },
        .mul_assign => {
            return try g.genAssign(node, .mul_assign, .mul);
        },
        .div_assign => {
            return try g.genAssign(node, .div_assign, .div);
        },
        .mod_assign => {
            return try g.genAssign(node, .mod_assign, .mod);
        },
        .call => {
            var data: Inst.Data = .{
                .call = .{
                    .callee = 0,
                    .this = 0,
                    .ret = 0,
                    .arg_offset = 0,
                    .argc = 0,
                },
            };

            var frame = try g.beginArgFrame();
            defer g.endArgFrame(&frame);

            if (node.data.call.callee.tag == .member_expr and node.data.call.callee.data.member.object.tag == .super_expr) {
                return try g.genSuperCall(node);
            }

            switch (node.data.call.callee.tag) {
                .identifier => {
                    data.call.callee = try g.genIdentifier(node.data.call.callee);
                    data.call.this = data.call.callee;
                },
                .member_expr => {
                    data.call.callee = try g.genMemberExpr(node.data.call.callee, &data.call.this);
                },
                .computed_member_expr => {
                    data.call.callee = try g.genComputedMemberExpr(node.data.call.callee, &data.call.this);
                },
                else => unreachable,
            }

            for (node.data.call.args) |arg| {
                const arg_value = try g.genExpr(arg);
                try g.pushArg(arg_value);
            }

            const info = try g.commitArgs(frame);

            data.call.arg_offset = info.offset;
            data.call.argc = @intCast(node.data.call.args.len);
            data.call.ret = g.allocRegister();

            try g.addInst(.call, data, node.loc);
            return data.call.ret;
        },
        .dict_expr => {
            const dict = g.allocRegister();
            try g.addUn(.build_dict, dict, node.loc);
            for (node.data.list) |entry| {
                const key = try g.genExpr(entry.data.dict_entry.key);
                const value = try g.genExpr(entry.data.dict_entry.value);
                try g.addTri(.add_dict_entry, key, value, dict, node.loc);
            }

            return dict;
        },
        .list_expr => {
            const list = g.allocRegister();
            try g.addUn(.build_list, list, node.loc);

            for (node.data.list) |item| {
                const value = try g.genExpr(item);
                try g.addBin(.append_list_item, value, list, node.loc);
            }

            return list;
        },
        .member_expr => {
            return try g.genMemberExpr(node, null);
        },
        .computed_member_expr => {
            return try g.genComputedMemberExpr(node, null);
        },
        .function_expr => return try g.genFunctionExpr(node, "<anonymous>"),
        else => {
            unreachable;
        },
    }
}

inline fn genAssign(g: *Generator, node: *const Ast.Node, tag: Ast.Node.Tag, op: Inst.Op) GenError!u32 {
    const value = try g.genExpr(node.data.bin.rhs);
    switch (node.data.bin.lhs.tag) {
        .member_expr => try g.genAssignMember(node.data.bin.lhs, value, tag, op),
        .computed_member_expr => try g.genAssignComputedMember(node.data.bin.lhs, value, tag, op),
        .identifier => try g.genAssignSimple(node, value, tag, op),
        else => {
            const str_obj = try String.new(g.gc, "Invalid assignment target");
            const exception = try Exception.new(g.gc, g.gc.interpreter.invalid_assignment_target_error_class, str_obj.as(String));
            const index = try g.addConstant(Value.object(Object.from(exception)));
            try g.addUn(.build_trace_and_throw_exception, index, node.loc);
        },
    }
    return value;
}

inline fn genSuperCall(g: *Generator, node: *const Ast.Node) GenError!u32 {
    const member = node.data.call.callee.data.member;

    const method = try g.addIdentifier(member.property);

    var data: Inst.Data = .{
        .super_call = .{
            .method = method,
            .ret = 0,
            .arg_offset = 0,
            .argc = 0,
        },
    };

    data.super_call.arg_offset = @intCast(g.extra.items.len);
    for (node.data.call.args) |arg| {
        const arg_value = try g.genExpr(arg);
        try g.extra.append(g.gpa, arg_value);
    }
    data.super_call.argc = @intCast(node.data.call.args.len);

    const ret = g.allocRegister();
    data.super_call.ret = ret;

    try g.addInst(.super_call, data, node.loc);

    return ret;
}

inline fn genFunctionExpr(g: *Generator, node: *const Ast.Node, name: []const u8) GenError!u32 {
    var func_gen = try Generator.init(g.gpa, g.ast, g.gc, g.string_interner);
    defer func_gen.deinit();
    func_gen.global_variables = g.global_variables;
    func_gen.scope_depth = 1;
    func_gen.enclosing = g;
    func_gen.gc.interpreter.generator = &func_gen;

    for (node.data.fndecl.params) |param| {
        try func_gen.declareParam(param);
    }
    try func_gen.genStmt(node.data.fndecl.body);

    try func_gen.addUn(.ret_none, func_gen.allocRegister(), node.loc);
    var executable = try func_gen.finalize();
    executable.name = try g.string_interner.intern(name);
    executable.param_count = @intCast(node.data.fndecl.params.len);

    const executable_index = try g.addConstant(Value.object(Object.from(executable)));
    const function_index = g.allocRegister();
    try g.addBin(.build_function, executable_index, function_index, node.loc);
    func_gen.gc.interpreter.generator = func_gen.enclosing;
    return function_index;
}

inline fn genIdentifier(g: *Generator, node: *const Ast.Node) GenError!u32 {
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
    try g.addBin(.load_global_by_name, identifier_index, dst, node.loc);
    return dst;
}

inline fn genComputedMemberExpr(g: *Generator, node: *const Ast.Node, this_reg: ?*u32) GenError!u32 {
    const object = try g.genExpr(node.data.bin.lhs);

    const index = try g.genExpr(node.data.bin.rhs);

    if (this_reg) |this| {
        this.* = object;
    }

    const dst = g.allocRegister();
    try g.addTri(.get_item, object, index, dst, node.loc);
    return dst;
}

inline fn genMemberExpr(g: *Generator, node: *const Ast.Node, this_reg: ?*u32) GenError!u32 {
    const object = try g.genExpr(node.data.member.object);
    const dst = g.allocRegister();
    const ident = try g.string_interner.intern(node.data.member.property);
    const key_index = try g.addConstant(Value.object(Object.from(ident)));
    try g.addTri(.get_attribute, object, key_index, dst, node.loc);
    if (this_reg) |this| {
        //TODO: be careful with this
        if (node.data.member.object.tag == .super_expr) {
            this.* = 0;
        } else {
            this.* = object;
        }
    }
    return dst;
}

inline fn genAssignSimple(g: *Generator, node: *const Ast.Node, value: u32, tag: Ast.Node.Tag, op: Inst.Op) GenError!void {
    const name = node.data.bin.lhs.data.string;

    var result_reg = value;
    if (tag != .assign) {
        const lhs_val = try g.genExpr(node.data.bin.lhs);
        result_reg = g.allocRegister();
        try g.addTri(op, lhs_val, value, result_reg, node.loc);
    }

    var variable: Variable = undefined;
    if (g.findVariable(name, &variable)) {
        try g.addBin(
            if (variable.scope == .global) .store_global_by_index else .mov,
            result_reg,
            variable.allocated_reg_slot,
            node.loc,
        );
    } else {
        const identifier_index = try g.addIdentifier(name);
        try g.addBin(.store_global_by_name, result_reg, identifier_index, node.loc);
    }
}

inline fn genAssignMember(g: *Generator, node: *const Ast.Node, value: u32, tag: Ast.Node.Tag, op: Inst.Op) GenError!void {
    const obj = try g.genExpr(node.data.member.object);
    const property_ident_index = try g.addIdentifier(node.data.member.property);

    var result_reg = value;
    if (tag != .assign) {
        const lhs_val = try g.genExpr(node);
        result_reg = g.allocRegister();
        try g.addTri(op, lhs_val, value, result_reg, node.loc);
    }

    const ident_reg = g.allocRegister();
    try g.addBin(.load_ident, property_ident_index, ident_reg, node.loc);
    try g.addTri(.set_attribute, ident_reg, result_reg, obj, node.loc);
}

inline fn genAssignComputedMember(g: *Generator, node: *const Ast.Node, value: u32, tag: Ast.Node.Tag, op: Inst.Op) GenError!void {
    const obj = try g.genExpr(node.data.bin.lhs);
    const computed_index = try g.genExpr(node.data.bin.rhs);

    var result_reg = value;
    if (tag != .assign) {
        const lhs_val = try g.genExpr(node);
        result_reg = g.allocRegister();
        try g.addTri(op, lhs_val, value, result_reg, node.loc);
    }

    try g.addTri(.set_item, obj, computed_index, result_reg, node.loc);
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

    for (executable.exception_handlers) |*handler_block| {
        handler_block.start_offset = block_start_offsets.items[handler_block.start_offset];
        handler_block.end_offset = handler_block.start_offset + handler_block.end_offset;
        for (handler_block.rescues) |*handler| {
            handler.handler_offset = block_start_offsets.items[handler.handler_offset];
            if (handler.ensure_offset) |ensure_offset| {
                handler.ensure_offset = block_start_offsets.items[ensure_offset];
            }
        }
    }

    executable.instructions = try instructions.toOwnedSlice(g.gpa);
    executable.spans = try spans.toOwnedSlice(g.gpa);
    executable.extra = try g.extra.toOwnedSlice(g.gpa);
}
