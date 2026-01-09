const std = @import("std");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const Interpreter = @import("Interpreter.zig");
const Executable = @import("../core/bytecode.zig").Executable;
const String = @import("String.zig");
const Function = @import("Function.zig");
const Inst = @import("../core/bytecode.zig").Inst;
const Error = Interpreter.Error;
const Exception = @import("Exception.zig");
const Gc = @import("../core/Gc.zig");
const Dict = @import("Dict.zig");
const List = @import("List.zig");
const Class = @import("Class.zig");
const NativeFunction = @import("NativeFunction.zig");

const Vm = @This();

pub const Globals = struct {
    fast_slots: []Value,
    named_slots: *Object,

    pub fn deinit(self: *Globals, gpa: std.mem.Allocator) void {
        gpa.free(self.fast_slots);
    }
};

const RegisterPool = struct {
    buffer: []Value,
    used: usize = 0,

    pub fn init(gpa: std.mem.Allocator, capacity: usize) !RegisterPool {
        return .{
            .buffer = try gpa.alloc(Value, capacity),
        };
    }

    pub fn deinit(self: *RegisterPool, gpa: std.mem.Allocator) void {
        gpa.free(self.buffer);
    }

    pub fn allocate(self: *RegisterPool, count: usize) ![]Value {
        // std.debug.print("registers used: {d}\n", .{self.used});
        const start = self.used;
        const end = start + count;
        if (end > self.buffer.len) {
            return Error.RegisterPoolExhausted;
        }
        self.used = end;
        return self.buffer[start..end];
    }

    pub fn deallocate(self: *RegisterPool, count: usize) void {
        std.debug.assert(self.used >= count);
        self.used -= count;
    }

    pub fn reset(self: *RegisterPool) void {
        self.used = 0;
    }
};

pub const ActivationRecord = struct {
    executable: *Executable,
    registers: []Value,
    ip: usize = 0,
    caller_return_reg: u32 = undefined,
    function: ?*Function = null,

    pub fn init(executable: *Executable, register_pool: *RegisterPool) !ActivationRecord {
        const registers = try register_pool.allocate(executable.max_register_count);
        return .{
            .executable = executable,
            .registers = registers,
        };
    }

    pub fn withFunction(register_pool: *RegisterPool, function: *Function) !ActivationRecord {
        const registers = try register_pool.allocate(function.exe.max_register_count);
        return .{
            .executable = function.exe,
            .registers = registers,
            .function = function,
        };
    }

    pub fn deinit(self: *ActivationRecord, register_pool: *RegisterPool) void {
        register_pool.deallocate(self.executable.max_register_count);
    }
};

const Records = std.ArrayList(ActivationRecord);

const default_register_pool_size = 100_000;
const default_record_capacity = 4096;

const max_records = 4096;

records: Records,
rp: usize = 0,
interpreter: *Interpreter,
gpa: std.mem.Allocator,
gc: *Gc,
register_pool: RegisterPool,

pub fn init(gpa: std.mem.Allocator, interpreter: *Interpreter) !*Vm {
    const vm = try gpa.create(Vm);
    const register_pool = try RegisterPool.init(gpa, default_register_pool_size);
    vm.* = .{
        .interpreter = interpreter,
        .gpa = gpa,
        .gc = &interpreter.gc,
        .records = try Records.initCapacity(gpa, max_records),
        .register_pool = register_pool,
    };
    return vm;
}

pub fn deinit(vm: *Vm) void {
    vm.records.deinit(vm.gpa);
    vm.register_pool.deinit(vm.gpa);
    vm.gpa.destroy(vm);
}

pub fn pushRecord(vm: *Vm, record: ActivationRecord) Error!Value {
    // std.debug.print("stack size: {d}\n", .{vm.records.items.len});
    if (vm.records.items.len >= max_records) {
        return vm.raiseException(
            vm.interpreter.stack_overflow_error_class,
            "maximum call stack size ({d}) exceeded",
            .{max_records},
        );
    }

    vm.records.appendAssumeCapacity(record);
    return Value.None;
}

pub fn runExecutable(vm: *Vm, executable: *Executable) Error!Value {
    const record = try ActivationRecord.init(executable, &vm.register_pool);
    _ = try vm.pushRecord(record);
    return try vm.runRecord(&vm.records.items[vm.records.items.len - 1], false);
}

const HandlerContext = struct {
    vm: *Vm,
    record: *ActivationRecord,
    registers: []Value,
    globals: *Globals,
    constants: []Value,
    identifiers: []Value,
    extra: []u32,
    return_value: Value = Value.None,
};

const Handler = *const fn (ctx: *HandlerContext, inst: Inst) Error!void;

const handlers = blk: {
    var table: [@typeInfo(Inst.Op).@"enum".fields.len]Handler = undefined;
    table[@intFromEnum(Inst.Op.load_const)] = handleLoadConst;
    table[@intFromEnum(Inst.Op.load_ident)] = handleLoadIdent;
    table[@intFromEnum(Inst.Op.load_true)] = handleLoadTrue;
    table[@intFromEnum(Inst.Op.load_false)] = handleLoadFalse;
    table[@intFromEnum(Inst.Op.load_none)] = handleLoadNone;
    table[@intFromEnum(Inst.Op.load_undefined)] = handleLoadUndefined;
    table[@intFromEnum(Inst.Op.store_global_by_index)] = handleStoreGlobalByIndex;
    table[@intFromEnum(Inst.Op.load_global_by_index)] = handleLoadGlobalByIndex;
    table[@intFromEnum(Inst.Op.store_global_by_name)] = handleStoreGlobalByName;
    table[@intFromEnum(Inst.Op.load_global_by_name)] = handleLoadGlobalByName;
    table[@intFromEnum(Inst.Op.mov)] = handleMov;

    table[@intFromEnum(Inst.Op.negate)] = handleNegate;
    table[@intFromEnum(Inst.Op.not)] = handleNot;
    table[@intFromEnum(Inst.Op.un_plus)] = handleUnPlus;

    table[@intFromEnum(Inst.Op.add)] = handleAdd;
    table[@intFromEnum(Inst.Op.sub)] = handleSub;
    table[@intFromEnum(Inst.Op.mul)] = handleMul;
    table[@intFromEnum(Inst.Op.div)] = handleDiv;
    table[@intFromEnum(Inst.Op.mod)] = handleModulus;
    table[@intFromEnum(Inst.Op.test_lt)] = handleTestLt;
    table[@intFromEnum(Inst.Op.test_gt)] = handleTestGt;
    table[@intFromEnum(Inst.Op.test_le)] = handleTestLe;
    table[@intFromEnum(Inst.Op.test_ge)] = handleTestGe;
    table[@intFromEnum(Inst.Op.test_eq)] = handleTestEq;
    table[@intFromEnum(Inst.Op.test_neq)] = handleTestNeq;

    table[@intFromEnum(Inst.Op.@"and")] = handleAnd;
    table[@intFromEnum(Inst.Op.@"or")] = handleOr;
    table[@intFromEnum(Inst.Op.xor)] = handleXor;

    table[@intFromEnum(Inst.Op.get_item)] = handleGetItem;
    table[@intFromEnum(Inst.Op.set_item)] = handleSetItem;
    table[@intFromEnum(Inst.Op.get_attribute)] = handleGetAttribute;
    table[@intFromEnum(Inst.Op.set_attribute)] = handleSetAttribute;

    table[@intFromEnum(Inst.Op.get_iter)] = handleGetIter;
    table[@intFromEnum(Inst.Op.iter_next)] = handleIterNext;
    table[@intFromEnum(Inst.Op.build_dict)] = handleBuildDict;
    table[@intFromEnum(Inst.Op.add_dict_entry)] = handleAddDictEntry;

    table[@intFromEnum(Inst.Op.build_function)] = handleBuildFunction;
    table[@intFromEnum(Inst.Op.build_class)] = handleBuildClass;
    table[@intFromEnum(Inst.Op.set_super_class)] = handleSetSuperClass;
    table[@intFromEnum(Inst.Op.add_class_method)] = handleAddClassMethod;

    table[@intFromEnum(Inst.Op.build_list)] = handleBuildList;
    table[@intFromEnum(Inst.Op.append_list_item)] = handleAppendListItem;

    table[@intFromEnum(Inst.Op.jmp)] = handleJmp;
    table[@intFromEnum(Inst.Op.branch)] = handleBranch;
    table[@intFromEnum(Inst.Op.build_trace_and_throw_exception)] = handleBuildTraceAndThrowException;

    table[@intFromEnum(Inst.Op.super_call)] = handleSuperCall;
    table[@intFromEnum(Inst.Op.call)] = handleCall;
    table[@intFromEnum(Inst.Op.ret)] = handleRet;
    table[@intFromEnum(Inst.Op.ret_none)] = handleRetNone;
    table[@intFromEnum(Inst.Op.raise_exception)] = handleRaiseException;
    table[@intFromEnum(Inst.Op.hlt)] = handleHlt;

    break :blk table;
};

pub fn runRecord(vm: *Vm, r: *ActivationRecord, _: bool) Error!Value {
    var ctx = HandlerContext{
        .vm = vm,
        .record = r,
        .registers = r.registers,
        .globals = vm.getGlobalSlots(),
        .constants = r.executable.constants,
        .identifiers = r.executable.identifiers,
        .extra = r.executable.extra,
    };

    const instructions = r.executable.instructions;

    loop: while (ctx.record.ip < instructions.len) {
        const inst = instructions[ctx.record.ip];
        ctx.record.ip += 1;
        dispatch(&ctx, inst) catch |err| {
            if (vm.interpreter.exception != null and vm.handleException(&ctx) == .Handled) {
                continue :loop;
            }
            return err;
        };
    }

    return ctx.return_value;
}

const ExceptionHandleResult = enum {
    Handled,
    NotHandled,
};

inline fn handleException(vm: *Vm, ctx: *HandlerContext) ExceptionHandleResult {
    const exception = vm.interpreter.exception.?;

    const handler_block =
        ctx.record.executable.findExceptionHandlerBlockForOffset(
            @intCast(ctx.record.ip - 1),
        ) orelse return .NotHandled;

    for (handler_block.rescues) |rescue_block| {
        const exception_class = switch (rescue_block.exeception_class_loc) {
            0 => ctx.registers[rescue_block.exception_type],
            1 => ctx.globals.fast_slots[rescue_block.exception_type],
            2 => blk: {
                const name =
                    ctx.identifiers[rescue_block.exception_type]
                        .toObject()
                        .toString();

                if (vm.interpreter.builtins.getField(name)) |field| {
                    break :blk field;
                }
                break :blk Value.None;
            },

            else => unreachable,
        };

        if (exception_class.asClass()) |class| {
            if (Object.from(exception).isInstanceOf(class)) {
                ctx.record.ip = rescue_block.handler_offset;
                return .Handled;
            }
        }
        // TODO: raise TypeError if exception_class is not a class
    }
    return .NotHandled;
}

fn dispatch(ctx: *HandlerContext, inst: Inst) Error!void {
    const handler = handlers[@intFromEnum(inst.op)];
    return handler(ctx, inst);
}

fn handleLoadConst(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.registers[inst.data.bin.rhs] = ctx.constants[inst.data.bin.lhs];
}

fn handleLoadIdent(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.registers[inst.data.bin.rhs] = ctx.identifiers[inst.data.bin.lhs];
}

fn handleLoadTrue(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.registers[inst.data.un] = Value.True;
}

fn handleLoadFalse(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.registers[inst.data.un] = Value.False;
}

fn handleLoadNone(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.registers[inst.data.un] = Value.None;
}

fn handleLoadUndefined(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.registers[inst.data.un] = Value.Undefined;
}

fn handleStoreGlobalByIndex(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.globals.fast_slots[inst.data.bin.rhs] = ctx.registers[inst.data.bin.lhs];
}

fn handleLoadGlobalByIndex(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.registers[inst.data.bin.rhs] = ctx.globals.fast_slots[inst.data.bin.lhs];
}

fn handleLoadGlobalByName(ctx: *HandlerContext, inst: Inst) Error!void {
    const name: *String = ctx.identifiers[inst.data.bin.lhs].toObject().as(String);
    if (ctx.vm.interpreter.builtins.getField(name)) |field| {
        ctx.registers[inst.data.bin.rhs] = field;
        return;
    }
    _ = try ctx.vm.raiseReferenceError("undeclared identifier '{s}'", .{name.asSlice()});
}

fn handleStoreGlobalByName(_: *HandlerContext, _: Inst) Error!void {}

fn handleMov(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.registers[inst.data.bin.rhs] = ctx.registers[inst.data.bin.lhs];
}

fn handleNegate(ctx: *HandlerContext, inst: Inst) Error!void {
    const value = ctx.registers[inst.data.bin.lhs];
    if (value.isNumeric()) {
        ctx.registers[inst.data.bin.rhs] = Value.number(-value.asNumber());
        return;
    }
    _ = try ctx.vm.raiseTypeError("invalid operand type for unary operator ('-') : '{s}'", .{value.getTypeString()});
}

fn handleNot(ctx: *HandlerContext, inst: Inst) Error!void {
    const value = ctx.registers[inst.data.bin.lhs];
    ctx.registers[inst.data.bin.rhs] = Value.bool(!value.isTruthy());
}

fn handleUnPlus(_: *HandlerContext, _: Inst) Error!void {
    //TODO: implement
}

fn handleAdd(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.number(lhs.asNumber() + rhs.asNumber());
        return;
    }

    if (lhs.isString() and rhs.isString()) {
        const a = lhs.toObject().toString();
        const b = rhs.toObject().toString();
        ctx.registers[inst.data.tri.dst] = Value.object(try a.concat(ctx.vm.gc, b));
        return;
    }

    if (lhs.isString() or rhs.isString()) {
        const self_str = if (lhs.isString())
            lhs.toObject().toString()
        else
            rhs.toObject().toString();

        const other_value = if (lhs.isString()) rhs else lhs;

        var other_str: *String = undefined;
        if (other_value.isObject()) {
            const s = try ctx.vm.invokeSpecialMethod(other_value, ctx.vm.interpreter.common_names.__str__, &[_]Value{});
            if (s.isString()) {
                other_str = s.toObject().toString();
            }
            _ = try ctx.vm.raiseTypeError("__str__ method returned non-string value", .{});
        } else {
            other_str = (try String.new(ctx.vm.gc, other_value.toString())).asString().?;
        }
        ctx.registers[inst.data.tri.dst] = Value.object(try self_str.concat(ctx.vm.gc, other_str));
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__add__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__radd__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    _ = try ctx.vm.raiseTypeException("+", lhs, rhs);
}

fn handleSub(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.number(lhs.asNumber() - rhs.asNumber());
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__sub__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__rsub__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    _ = try ctx.vm.raiseTypeException("-", lhs, rhs);
}

fn handleMul(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.number(lhs.asNumber() * rhs.asNumber());
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__mul__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__rmul__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    _ = try ctx.vm.raiseTypeException("*", lhs, rhs);
}

fn handleDiv(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        const lvalue = lhs.asNumber();
        const rvalue = rhs.asNumber();
        if (rvalue == 0.0) {
            _ = try ctx.vm.raiseZeroDivisionError("division by zero");
        }
        ctx.registers[inst.data.tri.dst] = Value.number(@divFloor(lvalue, rvalue));
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__div__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__rdiv__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    _ = try ctx.vm.raiseTypeException("/", lhs, rhs);
}

fn handleModulus(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        const lvalue = lhs.asNumber();
        const rvalue = rhs.asNumber();
        if (rvalue == 0.0) {
            _ = try ctx.vm.raiseZeroDivisionError("modulo by zero");
        }
        ctx.registers[inst.data.tri.dst] = Value.number(@mod(lvalue, rvalue));
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__mod__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__rmod__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    _ = try ctx.vm.raiseTypeException("%", lhs, rhs);
}

fn handleAnd(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];
    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.number(@floatFromInt(lhs.asInt() & rhs.asInt()));
        return;
    }
    _ = try ctx.vm.raiseTypeException("&", lhs, rhs);
}

fn handleOr(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];
    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.number(@floatFromInt(lhs.asInt() | rhs.asInt()));
        return;
    }
    _ = try ctx.vm.raiseTypeException("|", lhs, rhs);
}

fn handleXor(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];
    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.number(@floatFromInt(lhs.asInt() ^ rhs.asInt()));
        return;
    }
    _ = try ctx.vm.raiseTypeException("^", lhs, rhs);
}

fn handleTestLt(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.bool(lhs.asNumber() < rhs.asNumber());
        return;
    }

    if (lhs.isString() and rhs.isString()) {
        const ls = lhs.toObject().toString();
        const rs = rhs.toObject().toString();
        ctx.registers[inst.data.tri.dst] = Value.bool(ls.cmp(rs, .l));
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__lt__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__gt__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    _ = try ctx.vm.raiseTypeException("<", lhs, rhs);
}

fn handleTestLe(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.bool(lhs.asNumber() <= rhs.asNumber());
        return;
    }

    if (lhs.isString() and rhs.isString()) {
        const ls = lhs.toObject().toString();
        const rs = rhs.toObject().toString();
        ctx.registers[inst.data.tri.dst] = Value.bool(ls.cmp(rs, .le));
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__le__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__ge__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    _ = try ctx.vm.raiseTypeException("<=", lhs, rhs);
}

fn handleTestGt(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.bool(lhs.asNumber() > rhs.asNumber());
        return;
    }

    if (lhs.isString() and rhs.isString()) {
        const ls = lhs.toObject().toString();
        const rs = rhs.toObject().toString();
        ctx.registers[inst.data.tri.dst] = Value.bool(ls.cmp(rs, .g));
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__gt__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__lt__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    _ = try ctx.vm.raiseTypeException(">", lhs, rhs);
}

fn handleTestGe(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.bool(lhs.asNumber() >= rhs.asNumber());
        return;
    }

    if (lhs.isString() and rhs.isString()) {
        const ls = lhs.toObject().toString();
        const rs = rhs.toObject().toString();
        ctx.registers[inst.data.tri.dst] = Value.bool(ls.cmp(rs, .ge));
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__ge__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__le__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    _ = try ctx.vm.raiseTypeException(">=", lhs, rhs);
}

fn handleTestEq(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.bool(lhs.asNumber() == rhs.asNumber());
        return;
    }

    if (lhs.isString() and rhs.isString()) {
        const ls = lhs.toObject().toString();
        const rs = rhs.toObject().toString();
        ctx.registers[inst.data.tri.dst] = Value.bool(ls.cmp(rs, .eq));
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__eq__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__eq__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    ctx.registers[inst.data.tri.dst] = Value.bool(lhs.eql(rhs));
}

fn handleTestNeq(ctx: *HandlerContext, inst: Inst) Error!void {
    const lhs = ctx.registers[inst.data.tri.op1];
    const rhs = ctx.registers[inst.data.tri.op2];

    if (lhs.isNumeric() and rhs.isNumeric()) {
        ctx.registers[inst.data.tri.dst] = Value.bool(lhs.asNumber() != rhs.asNumber());
        return;
    }

    if (lhs.isString() and rhs.isString()) {
        const ls = lhs.toObject().toString();
        const rs = rhs.toObject().toString();
        ctx.registers[inst.data.tri.dst] = Value.bool(ls.cmp(rs, .ne));
        return;
    }

    if (lhs.isObject()) {
        const class = lhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__ne__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, lhs.toObject(), &[_]Value{rhs});
                return;
            }
        }
    }

    if (rhs.isObject()) {
        const class = rhs.toObject().class;
        if (class.getField(ctx.vm.interpreter.common_names.__ne__)) |method| {
            if (method.asObject()) |obj| {
                ctx.registers[inst.data.tri.dst] = try obj.callAssumeCallable(ctx.vm, rhs.toObject(), &[_]Value{lhs});
                return;
            }
        }
    }

    ctx.registers[inst.data.tri.dst] = Value.bool(!lhs.eql(rhs));
}

fn handleGetItem(ctx: *HandlerContext, inst: Inst) Error!void {
    const target = ctx.registers[inst.data.tri.op1];
    const index = ctx.registers[inst.data.tri.op2];

    var args: [1]Value = undefined;
    args[0] = index;

    ctx.registers[inst.data.tri.dst] = try ctx.vm.invokeSpecialMethod(
        target,
        ctx.vm.interpreter.common_names.__getitem__,
        &args,
    );
}

fn handleSetItem(ctx: *HandlerContext, inst: Inst) Error!void {
    const target = ctx.registers[inst.data.tri.op1];
    const index = ctx.registers[inst.data.tri.op2];

    var args: [2]Value = undefined;
    args[0] = index;
    args[1] = ctx.registers[inst.data.tri.dst];

    _ = try ctx.vm.invokeSpecialMethod(
        target,
        ctx.vm.interpreter.common_names.__setitem__,
        &args,
    );
}

fn handleGetAttribute(ctx: *HandlerContext, inst: Inst) Error!void {
    const target = ctx.registers[inst.data.tri.op1];
    const index = ctx.constants[inst.data.tri.op2];

    var args: [1]Value = undefined;
    args[0] = index;

    ctx.registers[inst.data.tri.dst] = try ctx.vm.invokeSpecialMethod(
        target,
        ctx.vm.interpreter.common_names.__getattr__,
        &args,
    );
}

fn handleSetAttribute(ctx: *HandlerContext, inst: Inst) Error!void {
    const target = ctx.registers[inst.data.tri.dst];
    const index = ctx.registers[inst.data.tri.op2];

    var args: [2]Value = undefined;
    args[1] = index;
    args[0] = ctx.registers[inst.data.tri.op1];

    _ = try ctx.vm.invokeSpecialMethod(
        target,
        ctx.vm.interpreter.common_names.__setattr__,
        &args,
    );
}

fn handleGetIter(ctx: *HandlerContext, inst: Inst) Error!void {
    const iterable_value = ctx.registers[inst.data.bin.lhs];
    ctx.registers[inst.data.bin.rhs] = try ctx.vm.invokeSpecialMethod(
        iterable_value,
        ctx.vm.interpreter.common_names.__iter__,
        &[_]Value{},
    );
}

fn handleIterNext(ctx: *HandlerContext, inst: Inst) Error!void {
    const iterator_value = ctx.registers[inst.data.bin.lhs];
    ctx.registers[inst.data.bin.rhs] = try ctx.vm.invokeSpecialMethod(
        iterator_value,
        ctx.vm.interpreter.common_names.__next__,
        &[_]Value{},
    );
}

fn handleBuildDict(ctx: *HandlerContext, inst: Inst) Error!void {
    const dict = try Dict.new(ctx.vm.gc);
    ctx.registers[inst.data.un] = Value.object(dict);
}

fn handleAddDictEntry(ctx: *HandlerContext, inst: Inst) Error!void {
    const dict: *Dict = ctx.registers[inst.data.tri.dst].toObject().as(Dict);
    const key = ctx.registers[inst.data.tri.op1];
    const value = ctx.registers[inst.data.tri.op2];
    try dict.set(ctx.vm, key, value);
}

fn handleBuildFunction(ctx: *HandlerContext, inst: Inst) Error!void {
    const func_obj = try Function.withExecutable(ctx.vm.gc, ctx.constants[inst.data.bin.lhs].toObject().as(Executable));
    const function: *Function = func_obj.as(Function);
    function.module_env = ctx.vm.interpreter.getRunningModule();
    ctx.registers[inst.data.bin.rhs] = Value.object(func_obj);
}

fn handleBuildClass(ctx: *HandlerContext, inst: Inst) Error!void {
    const class_name = ctx.identifiers[inst.data.bin.lhs];
    const class = try Class.new(ctx.vm.gc);
    class.super_class = ctx.vm.interpreter.base_class;
    class.name = class_name.toObject().toString();
    Object.from(class).class = ctx.vm.interpreter.base_class;
    ctx.registers[inst.data.bin.rhs] = Value.object(Object.from(class));
}

fn handleSetSuperClass(ctx: *HandlerContext, inst: Inst) Error!void {
    const sub_class_value = ctx.registers[inst.data.bin.rhs];
    const super_class_value = ctx.registers[inst.data.bin.lhs];

    if (super_class_value.asClass()) |super_class| {
        const sub_class: *Class = sub_class_value.toObject().as(Class);
        sub_class.super_class = super_class;
        return;
    }

    _ = try ctx.vm.raiseTypeError("Expected a class", .{});
}

fn handleAddClassMethod(ctx: *HandlerContext, inst: Inst) Error!void {
    const class: *Class = ctx.registers[inst.data.bin.rhs].toObject().as(Class);
    const method: *Function = ctx.registers[inst.data.bin.lhs].toObject().as(Function);
    method.home_class = class;
    try class.addMethod(method.exe.name, ctx.registers[inst.data.bin.lhs]);
}

fn handleBuildList(ctx: *HandlerContext, inst: Inst) Error!void {
    const list = try List.new(ctx.vm.gc);
    ctx.registers[inst.data.un] = Value.object(list);
}

fn handleAppendListItem(ctx: *HandlerContext, inst: Inst) Error!void {
    const list: *List = ctx.registers[inst.data.bin.rhs].toObject().as(List);
    const item = ctx.registers[inst.data.bin.lhs];
    try list.append(item);
}

fn handleJmp(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.record.ip = inst.data.un;
}

fn handleBranch(ctx: *HandlerContext, inst: Inst) Error!void {
    if (ctx.registers[inst.data.tri.op1].isTruthy()) {
        ctx.record.ip = inst.data.tri.op2;
    } else {
        ctx.record.ip = inst.data.tri.dst;
    }
}

fn handleBuildTraceAndThrowException(ctx: *HandlerContext, inst: Inst) Error!void {
    const exception: *Exception = ctx.constants[inst.data.un].toObject().as(Exception);
    try exception.buildTraceback(ctx.vm);
    ctx.vm.interpreter.exception = exception;
    return Error.ExceptionThrown;
}

fn handleSuperCall(ctx: *HandlerContext, inst: Inst) Error!void {
    const func = ctx.record.function.?;
    const home_class = func.home_class.?;
    const super_class = home_class.super_class.?;

    const method_name = ctx.identifiers[inst.data.super_call.method].toObject().toString();
    const method_value = super_class.getField(method_name);
    const this_value = ctx.registers[0];

    if (method_value) |meth_val| {
        if (meth_val.asObject()) |meth_obj| {
            const args_data = ctx.extra[inst.data.super_call.arg_offset .. inst.data.super_call.arg_offset + inst.data.super_call.argc];

            var args: [8]Value = undefined;
            for (args_data, 0..) |arg, i| {
                args[i] = ctx.registers[arg];
            }

            ctx.registers[inst.data.super_call.ret] = try meth_obj.callAssumeCallable(ctx.vm, this_value.toObject(), args[0..inst.data.super_call.argc]);
            return;
        }
    }

    _ = try ctx.vm.raiseAttributeError(
        "'{s}' has no method '{s}'",
        .{ super_class.name.asSlice(), method_name.asSlice() },
    );
}

fn handleCall(ctx: *HandlerContext, inst: Inst) Error!void {
    const callee = ctx.registers[inst.data.call.callee];
    if (!callee.isObject()) {
        _ = try ctx.vm.raiseTypeError("'{s}' is not callable", .{callee.getTypeString()});
        return;
    }

    const callee_obj = callee.toObject();
    if (!callee_obj.isFunction()) {
        _ = try ctx.vm.raiseTypeError("'{s}' object is not callable", .{callee_obj.getClassName()});
        return;
    }

    const this_value = ctx.registers[inst.data.call.this];
    const args_data = ctx.extra[inst.data.call.arg_offset .. inst.data.call.arg_offset + inst.data.call.argc];

    if (callee_obj.asNativeFunction()) |native_function| {
        _ = try ctx.vm.checkArity(NativeFunction, native_function.name, native_function.arity, @intCast(inst.data.call.argc), native_function.isVariadic);

        var args: [8]Value = undefined;
        for (args_data, 0..) |arg, i| {
            args[i] = ctx.registers[arg];
        }

        ctx.registers[inst.data.call.ret] = try native_function.function(ctx.vm, this_value.toObject(), args[0..inst.data.call.argc]);
        return;
    }

    if (callee_obj.asClass()) |class| {
        var args: [8]Value = undefined;
        for (args_data, 0..) |arg, i| {
            args[i] = ctx.registers[arg];
        }

        if (class.constructor) |constructor| {
            ctx.registers[inst.data.call.ret] = try constructor(ctx.vm, this_value.toObject(), args[0..inst.data.call.argc]);
        } else if (class.getField(ctx.vm.interpreter.common_names.__constructor__)) |new_method| {
            ctx.registers[inst.data.call.ret] = try new_method.toObject().callAssumeCallable(ctx.vm, Object.from(class), args[0..inst.data.call.argc]);
        } else {
            ctx.registers[inst.data.call.ret] = Value.object(try class.newInstance(ctx.vm.gc));
        }

        if (class.getField(ctx.vm.interpreter.common_names.__init__)) |init_method| {
            _ = try init_method.toObject().callAssumeCallable(ctx.vm, ctx.registers[inst.data.call.ret].toObject(), args[0..inst.data.call.argc]);
        }
        return;
    }

    const function: *Function = callee_obj.as(Function);
    _ = try ctx.vm.checkArity(Function, function.exe.name, function.arity, @intCast(inst.data.call.argc), false);

    var new_record = try ActivationRecord.withFunction(&ctx.vm.register_pool, function);

    new_record.registers[0] = this_value;
    for (args_data, 0..) |arg, i| {
        new_record.registers[i + 1] = ctx.registers[arg];
    }

    _ = try ctx.vm.pushRecord(new_record);
    const result = try ctx.vm.runRecord(&ctx.vm.records.items[ctx.vm.records.items.len - 1], false);

    var executed_record = ctx.vm.records.pop().?;
    executed_record.deinit(&ctx.vm.register_pool);

    ctx.registers[inst.data.call.ret] = result;
}

fn handleRet(ctx: *HandlerContext, inst: Inst) Error!void {
    ctx.return_value = ctx.registers[inst.data.un];
    ctx.record.ip = ctx.record.executable.instructions.len;
}

fn handleRetNone(ctx: *HandlerContext, _: Inst) Error!void {
    ctx.record.ip = ctx.record.executable.instructions.len;
}

fn handleRaiseException(ctx: *HandlerContext, inst: Inst) Error!void {
    const exception_val = ctx.registers[inst.data.un];
    if (exception_val.asObject()) |exception_object| {
        if (exception_object.isInstanceOf(ctx.vm.interpreter.base_exception_class)) {
            ctx.vm.interpreter.exception = exception_object.asException().?;
            return Error.ExceptionThrown;
        }
        _ = try ctx.vm.raiseTypeError("raise requires an exception object", .{});
        return;
    }
    _ = try ctx.vm.raiseTypeError("raise requires an exception object", .{});
}

fn handleHlt(ctx: *HandlerContext, inst: Inst) Error!void {
    _ = inst;
    ctx.record.ip = ctx.record.executable.instructions.len;
}

pub inline fn checkArity(vm: *Vm, comptime T: anytype, name: *String, expected: u8, actual: u8, variadic: bool) !Value {
    comptime {
        if (!@hasField(T, "arity")) {
            @compileError("Callable must define arity");
        }
    }

    const check = switch (T) {
        NativeFunction => !variadic and actual != expected,
        Function => actual != expected,
        else => unreachable,
    };

    if (check) {
        return try vm.raiseTypeError(
            "'{s}' expects {d} arguments but got {d}",
            .{ name.asSlice(), expected, actual },
        );
    }

    return Value.None;
}

inline fn invokeSpecialMethod(
    vm: *Vm,
    target: Value,
    method_name: *String,
    args: []const Value,
) Error!Value {
    //TODO: refactor
    if (!target.isObject()) {
        return vm.raiseTypeError(
            "'invalid type '{s}' for operation '{s}'",
            .{ target.getTypeString(), method_name.asSlice() },
        );
    }

    const cls = target.toObject().class;
    const method = cls.getField(method_name) orelse {
        return vm.raiseTypeError(
            "'{s}' object does not support special method '{s}'",
            .{ target.getTypeString(), method_name.asSlice() },
        );
    };

    const callable = method.asObject() orelse {
        return vm.raiseTypeError(
            "{s} has to be callable object",
            .{method.getTypeString()},
        );
    };

    return callable.callAssumeCallable(
        vm,
        target.toObject(),
        args,
    );
}

pub inline fn raiseTypeException(vm: *Vm, comptime op: []const u8, lhs: Value, rhs: Value) Error!Value {
    return vm.raiseException(vm.interpreter.type_error_class, "invalid operand types for operation (\"{s}\"): {s} and {s}", .{ op, lhs.getTypeString(), rhs.getTypeString() });
}

pub inline fn raiseException(vm: *Vm, class: *Class, comptime fmt: []const u8, args: anytype) Error!Value {
    vm.interpreter.exception = try Exception.withMessage(vm, class, fmt, args);
    return Error.ExceptionThrown;
}

pub inline fn getGlobalSlots(vm: *Vm) *Vm.Globals {
    std.debug.assert(vm.records.items.len > 0);
    const func = vm.records.getLast().function;
    if (func) |f| {
        return &f.module_env.globals;
    }
    return vm.interpreter.getGlobalSlots();
}

pub inline fn raiseTypeError(
    vm: *Vm,
    comptime fmt: []const u8,
    args: anytype,
) Error!Value {
    return vm.raiseException(vm.interpreter.type_error_class, fmt, args);
}

pub inline fn raiseAttributeError(
    vm: *Vm,
    comptime fmt: []const u8,
    args: anytype,
) Error!Value {
    return vm.raiseException(vm.interpreter.attribute_error_class, fmt, args);
}

pub inline fn raiseZeroDivisionError(vm: *Vm, comptime msg: []const u8) Error!Value {
    return vm.raiseException(
        vm.interpreter.zero_division_error_class,
        msg,
        .{},
    );
}

pub inline fn raiseReferenceError(
    vm: *Vm,
    comptime fmt: []const u8,
    args: anytype,
) Error!Value {
    return vm.raiseException(vm.interpreter.reference_error_class, fmt, args);
}

pub inline fn raiseValueError(
    vm: *Vm,
    comptime fmt: []const u8,
    args: anytype,
) Error!Value {
    return vm.raiseException(vm.interpreter.value_error_class, fmt, args);
}

pub inline fn raiseModuleNotFoundError(
    vm: *Vm,
    comptime fmt: []const u8,
    args: anytype,
) Error!Value {
    return vm.raiseException(vm.interpreter.module_not_found_error_class, fmt, args);
}

pub inline fn raiseIndexError(
    vm: *Vm,
    comptime fmt: []const u8,
    args: anytype,
) Error!Value {
    return vm.raiseException(vm.interpreter.index_error_class, fmt, args);
}
