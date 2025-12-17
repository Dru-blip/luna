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

const Vm = @This();

const Globals = struct {
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

const ActivationRecord = struct {
    executable: *Executable,
    registers: []Value,
    ip: usize = 0,
    caller_return_reg: u32 = undefined,

    pub fn init(executable: *Executable, register_pool: *RegisterPool) !ActivationRecord {
        const registers = try register_pool.allocate(executable.max_register_count);
        return .{
            .executable = executable,
            .registers = registers,
        };
    }

    pub fn deinit(self: *ActivationRecord, register_pool: *RegisterPool) void {
        register_pool.deallocate(self.executable.max_register_count);
    }
};

const Records = std.ArrayList(ActivationRecord);

const default_register_pool_size = 100_000;
const default_record_capacity = 2048;

records: Records = .empty,
rp: usize = 0,
interpreter: *Interpreter,
globals: Globals = undefined,
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
        .records = try Records.initCapacity(gpa, default_record_capacity),
        .register_pool = register_pool,
    };
    return vm;
}

pub fn deinit(vm: *Vm) void {
    vm.globals.deinit(vm.gpa);
    vm.records.deinit(vm.gpa);
    vm.gpa.destroy(vm);
}

pub fn runExecutable(vm: *Vm, executable: *Executable) Error!Value {
    const record = try ActivationRecord.init(executable, &vm.register_pool);
    vm.globals.fast_slots = try vm.gpa.alloc(Value, executable.global_variable_count);
    vm.records.appendAssumeCapacity(record);
    return try vm.runRecord(&vm.records.items[vm.records.items.len - 1], false);
}

pub fn runRecord(vm: *Vm, r: *ActivationRecord, as_callback: bool) Error!Value {
    _ = as_callback;
    var record = r;
    var registers = record.registers;
    var globals = &vm.globals;
    var instructions = record.executable.instructions;
    var constants = record.executable.constants;
    var instruction: Inst = undefined;

    start: while (true) {
        instruction = instructions[record.ip];
        const data = instruction.data;
        record.ip += 1;
        switch (instruction.op) {
            .load_const => {
                registers[data.bin.rhs] = constants[data.bin.lhs];
                continue :start;
            },
            .load_true => {
                registers[data.un] = Value.bool(true);
                continue :start;
            },
            .load_false => {
                registers[data.un] = Value.bool(false);
                continue :start;
            },
            .load_none => {
                registers[data.un] = Value.none();
                continue :start;
            },
            .store_global_by_index => {
                globals.fast_slots[data.bin.rhs] = registers[data.bin.lhs];
                continue :start;
            },
            .load_global_by_index => {
                registers[data.bin.rhs] = globals.fast_slots[data.bin.lhs];
                continue :start;
            },
            .load_global_by_name => {
                continue :start;
            },
            .store_global_by_name => {
                continue :start;
            },
            .mov => {
                registers[data.bin.rhs] = registers[data.bin.lhs];
                continue :start;
            },
            .add => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.int(lhs.toInt() + rhs.toInt());
                    continue :start;
                }

                return vm.raiseTypeException("+", lhs, rhs);
            },
            .sub => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.int(lhs.toInt() - rhs.toInt());
                    continue :start;
                }

                return vm.raiseTypeException("-", lhs, rhs);
            },
            .mul => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.int(lhs.toInt() * rhs.toInt());
                    continue :start;
                }
                return vm.raiseTypeException("*", lhs, rhs);
            },
            .div => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    if (rhs.toInt() == 0) {
                        //TODO: throw error for division by zero
                    }
                    registers[data.tri.dst] = Value.int(@divFloor(lhs.toInt(), rhs.toInt()));
                    continue :start;
                }
                return vm.raiseTypeException("/", lhs, rhs);
            },
            .mod => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    if (rhs.toInt() == 0) {
                        //TODO: throw error for division by zero
                    }
                    registers[data.tri.dst] = Value.int(@mod(lhs.toInt(), rhs.toInt()));
                    continue :start;
                }
                return vm.raiseTypeException("%", lhs, rhs);
            },
            .test_lt => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() < rhs.toInt());
                    continue :start;
                }
                return vm.raiseTypeException("<", lhs, rhs);
            },
            .test_le => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() <= rhs.toInt());
                    continue :start;
                }
                return vm.raiseTypeException("<=", lhs, rhs);
            },
            .test_gt => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() > rhs.toInt());
                    continue :start;
                }
                return vm.raiseTypeException(">", lhs, rhs);
            },
            .test_ge => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() >= rhs.toInt());
                    continue :start;
                }
                return vm.raiseTypeException(">=", lhs, rhs);
            },
            .test_eq => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() == rhs.toInt());
                    continue :start;
                }
                return vm.raiseTypeException("==", lhs, rhs);
            },
            .test_neq => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() != rhs.toInt());
                    continue :start;
                }
                return vm.raiseTypeException("!=", lhs, rhs);
            },
            .build_function => {
                const func = try Function.withExecutable(vm.gc, constants[data.bin.lhs].toObject().as(Executable));
                registers[data.bin.rhs] = Value.object(Object.from(func));
                continue :start;
            },
            .object_create => {
                const obj = try Object.new(vm.gc);
                registers[data.un] = Value.object(obj);
                continue :start;
            },
            .object_set_property => {
                const val = registers[data.tri.dst];
                const key = registers[data.tri.op1];
                const value = registers[data.tri.op2];
                if (val.asObject()) |obj| {
                    //TODO: currently we assume any value is a valid property key
                    try obj.set(key.toPropertyKey().?, value);
                }
                continue :start;
            },
            .object_get_property => {
                const obj_val = registers[data.tri.op1];
                const key: *String = constants[data.tri.op2].toObject().as(String);

                if (obj_val.asObject()) |object| {
                    if (object.get(key.toPropertyKey())) |v| {
                        registers[data.tri.dst] = v;
                        continue :start;
                    }
                    return vm.raiseException(.property_error, "{s}", .{key.asSlice()});
                }
                return vm.raiseException(.type_error, "{s} is not subscriptable", .{obj_val.getTypeString()});
            },
            .object_subscript => {
                const val = registers[data.tri.op1];
                const property = registers[data.tri.op2];
                if (val.asObject()) |obj| {
                    //TODO: handle string property keys
                    if (property.isInt() and property.toInt() >= 0) {
                        if (obj.get(property.toPropertyKey().?)) |v| {
                            registers[data.tri.dst] = v;
                            continue :start;
                        }
                        return vm.raiseException(.property_error, "{d}", .{property.toInt()});
                    }
                    return vm.raiseException(.property_error, "invalid property key type: {s}", .{property.getTypeString()});
                }
                return vm.raiseException(.type_error, "{s} is not subscriptable", .{val.getTypeString()});
            },
            .jmp => {
                record.ip = data.un;
                continue :start;
            },
            .branch => {
                if (registers[data.tri.op1].isTruthy()) {
                    record.ip = data.tri.op2;
                } else {
                    record.ip = data.tri.dst;
                }
                continue :start;
            },
            .call => {
                const callee = registers[data.call.callee];
                if (!callee.isObject()) {
                    return vm.raiseException(.type_error, "non callable value", .{});
                }
                const callee_obj = callee.toObject();
                if (!callee_obj.isFunction()) {
                    return vm.raiseException(.type_error, "non callable object", .{});
                }
                const function: *Function = callee_obj.as(Function);
                const parent_record = vm.records.getLast();
                vm.records.appendAssumeCapacity(try ActivationRecord.init(function.data.executable, &vm.register_pool));
                record = &vm.records.items[vm.records.items.len - 1];
                record.caller_return_reg = data.call.ret;

                for (data.call.args, 0..) |arg, i| {
                    record.registers[i] = parent_record.registers[arg];
                }

                registers = record.registers;
                instructions = record.executable.instructions;
                constants = record.executable.constants;
                continue :start;
            },
            .ret => {
                var child_record = vm.records.pop().?;
                if (vm.records.items.len == 0) {
                    return registers[data.un];
                }
                record = &vm.records.items[vm.records.items.len - 1];

                registers = record.registers;
                instructions = record.executable.instructions;
                constants = record.executable.constants;
                record.registers[child_record.caller_return_reg] = child_record.registers[data.un];
                child_record.deinit(&vm.register_pool);
                continue :start;
            },
            .ret_none => {
                return Value.none();
            },
            .hlt => {
                return Value.none();
            },
        }
    }

    return Value.none();
}

pub inline fn raiseException(vm: *Vm, tag: Exception.Tag, comptime fmt: []const u8, args: anytype) Error!Value {
    vm.interpreter.exception = try Exception.withMessage(vm, tag, fmt, args);
    return Error.ExceptionThrown;
}

pub fn raiseTypeException(vm: *Vm, comptime op: []const u8, lhs: Value, rhs: Value) Error!Value {
    return vm.raiseException(.type_error, "invalid operand types for operation (\"{s}\"): {s} and {s}", .{ op, lhs.getTypeString(), rhs.getTypeString() });
}
