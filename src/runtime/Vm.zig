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

pub const ActivationRecord = struct {
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
    var record = r;
    var registers = record.registers;
    var globals = &vm.globals;
    var instructions = record.executable.instructions;
    var identifiers = record.executable.identifiers;
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
            .load_ident => {
                registers[data.bin.rhs] = identifiers[data.bin.lhs];
                continue :start;
            },
            .load_true => {
                registers[data.un] = Value.True;
                continue :start;
            },
            .load_false => {
                registers[data.un] = Value.False;
                continue :start;
            },
            .load_none => {
                registers[data.un] = Value.None;
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
                const name: *String = identifiers[data.bin.lhs].toObject().as(String);
                if (vm.interpreter.builtins.getField(name)) |field| {
                    registers[data.bin.rhs] = field;
                    continue :start;
                }
                return vm.raiseException(.reference_error, "undeclared identifier '{s}'", .{name.asSlice()});
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

                // Float
                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.number(lhs.asNumber() + rhs.asNumber());
                    continue :start;
                }

                return vm.raiseTypeException("+", lhs, rhs);
            },
            .sub => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.number(lhs.asNumber() - rhs.asNumber());
                    continue :start;
                }

                return vm.raiseTypeException("-", lhs, rhs);
            },
            .mul => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.number(lhs.asNumber() * rhs.asNumber());
                    continue :start;
                }

                return vm.raiseTypeException("*", lhs, rhs);
            },
            .div => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    const lvalue = lhs.asNumber();
                    const rvalue = rhs.asNumber();
                    if (rvalue == 0.0) {
                        return vm.raiseException(.zero_division_error, "division by zero", .{});
                    }
                    registers[data.tri.dst] = Value.number(@divFloor(lvalue, rvalue));
                    continue :start;
                }

                return vm.raiseTypeException("/", lhs, rhs);
            },
            .mod => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    const lvalue = lhs.asNumber();
                    const rvalue = rhs.asNumber();
                    if (rvalue == 0.0) {
                        return vm.raiseException(.zero_division_error, "modulo by zero", .{});
                    }
                    registers[data.tri.dst] = Value.number(@mod(lvalue, rvalue));
                    continue :start;
                }

                return vm.raiseTypeException("%", lhs, rhs);
            },
            .test_lt => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.bool(lhs.asNumber() < rhs.asNumber());
                    continue :start;
                }

                return vm.raiseTypeException("<", lhs, rhs);
            },
            .test_le => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.bool(lhs.asNumber() <= rhs.asNumber());
                    continue :start;
                }

                return vm.raiseTypeException("<=", lhs, rhs);
            },
            .test_gt => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.bool(lhs.asNumber() > rhs.asNumber());
                    continue :start;
                }
                return vm.raiseTypeException(">", lhs, rhs);
            },
            .test_ge => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.bool(lhs.asNumber() >= rhs.asNumber());
                    continue :start;
                }

                return vm.raiseTypeException(">=", lhs, rhs);
            },
            .test_eq => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.bool(lhs.asNumber() == rhs.asNumber());
                    continue :start;
                }

                registers[data.tri.dst] = Value.bool(lhs.eql(rhs));
                continue :start;
            },
            .test_neq => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.bool(lhs.asNumber() != rhs.asNumber());
                    continue :start;
                }

                registers[data.tri.dst] = Value.bool(!lhs.eql(rhs));
                continue :start;
            },
            .build_function => {
                const func = try Function.withExecutable(vm.gc, constants[data.bin.lhs].toObject().as(Executable));
                registers[data.bin.rhs] = Value.object(func);
                continue :start;
            },
            .build_trace_and_throw_exception => {
                const exception: *Exception = constants[data.un].toObject().as(Exception);
                try exception.buildTraceback(vm);
                vm.interpreter.exception = exception;
                return Error.ExceptionThrown;
            },
            .build_dict => {
                const dict = try Dict.new(vm.gc);
                registers[data.un] = Value.object(dict);
                continue :start;
            },
            .add_dict_entry => {
                const dict: *Dict = registers[data.tri.dst].toObject().as(Dict);
                const key = registers[data.tri.op1];
                const value = registers[data.tri.op2];
                try dict.set(key, value);
                continue :start;
            },
            .object_create => {
                const obj = try Object.new(vm.gc);
                registers[data.un] = Value.object(obj);
                continue :start;
            },
            .object_set_property => {
                const val = registers[data.tri.dst];
                _ = registers[data.tri.op1];
                _ = registers[data.tri.op2];
                if (val.asObject()) |_| {
                    //TODO: currently we assume any value is a valid property key
                    // try obj.set(key.toPropertyKey().?, value);
                }
                continue :start;
            },
            .object_get_property => {
                const obj_val = registers[data.tri.op1];
                const key: *String = constants[data.tri.op2].toObject().as(String);

                if (obj_val.asObject()) |_| {
                    // if (object.get(key.toPropertyKey())) |v| {
                    //     registers[data.tri.dst] = v;
                    //     continue :start;
                    // }
                    return vm.raiseException(.property_error, "{s}", .{key.asSlice()});
                }
                return vm.raiseException(.type_error, "{s} is not subscriptable", .{obj_val.getTypeString()});
            },
            .object_subscript_get => {
                const val = registers[data.tri.op1];
                const property = registers[data.tri.op2];
                if (val.asObject()) |_| {
                    // if (property.toPropertyKey()) |key| {
                    //     if (obj.get(key)) |v| {
                    //         registers[data.tri.dst] = v;
                    //         continue :start;
                    //     }
                    //     return vm.raiseException(.property_error, "{s}", .{property.getTypeString()});
                    // }
                    return vm.raiseException(.property_error, "invalid property key type: {s}", .{property.getTypeString()});
                }
                return vm.raiseException(.type_error, "{s} is not subscriptable", .{val.getTypeString()});
            },
            .object_subscript_set => {
                _ = registers[data.tri.dst];
                const obj = registers[data.tri.op1];
                const index = registers[data.tri.op2];
                if (obj.asObject()) |_| {
                    return vm.raiseException(.property_error, "invalid property key type: {s}", .{index.getTypeString()});
                }
                return vm.raiseException(.type_error, "{s} is not subscriptable", .{obj.getTypeString()});
            },
            .get_item => {
                const target = registers[data.tri.op1];
                const index = registers[data.tri.op2];

                const target_class = target.toObject().class;
                const getitem_method = target_class.getField(try vm.interpreter.string_interner.intern("__getitem__"));

                if (getitem_method) |method_value| {
                    var args: [8]Value = undefined;
                    args[0] = index;

                    if (method_value.asObject()) |callable_obj| {
                        const retvalue = try callable_obj.callAssumeCallable(
                            vm,
                            target.toObject(),
                            &args,
                        );
                        registers[data.tri.dst] = retvalue;
                        continue :start;
                    }
                }

                return vm.raiseException(.type_error, "{s} is not subscriptable", .{target.getTypeString()});
            },
            .set_item => {},
            .get_attribute => {},
            .set_attribute => {},
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

                const this_value = registers[data.call.this];
                if (callee_obj.asNativeFunction()) |native_function| {
                    const args = try vm.gpa.alloc(Value, data.call.args.len);
                    defer vm.gpa.free(args);
                    for (data.call.args, 0..) |arg, i| {
                        args[i] = record.registers[arg];
                    }
                    registers[data.call.ret] = try native_function.function(vm, this_value.toObject(), args);
                    continue :start;
                }

                const function: *Function = callee_obj.as(Function);
                const caller_record = vm.records.getLast();
                vm.records.appendAssumeCapacity(try ActivationRecord.init(function.data.executable, &vm.register_pool));
                record = &vm.records.items[vm.records.items.len - 1];
                record.caller_return_reg = data.call.ret;

                record.registers[0] = this_value;
                for (data.call.args, 0..) |arg, i| {
                    record.registers[i + 1] = caller_record.registers[arg];
                }

                registers = record.registers;
                instructions = record.executable.instructions;
                constants = record.executable.constants;
                identifiers = record.executable.identifiers;
                continue :start;
            },
            .ret => {
                var callee = vm.records.pop().?;
                if (vm.records.items.len == 0 or as_callback) {
                    return registers[data.un];
                }
                record = &vm.records.items[vm.records.items.len - 1];

                registers = record.registers;
                instructions = record.executable.instructions;
                constants = record.executable.constants;
                identifiers = record.executable.identifiers;
                record.registers[callee.caller_return_reg] = callee.registers[data.un];
                callee.deinit(&vm.register_pool);
                continue :start;
            },
            .ret_none => {
                var callee = vm.records.pop().?;
                if (vm.records.items.len == 0 or as_callback) {
                    return Value.None;
                }
                record = &vm.records.items[vm.records.items.len - 1];

                registers = record.registers;
                instructions = record.executable.instructions;
                constants = record.executable.constants;
                identifiers = record.executable.identifiers;
                record.registers[callee.caller_return_reg] = Value.None;
                callee.deinit(&vm.register_pool);
                continue :start;
            },
            .hlt => {
                return Value.None;
            },
        }
    }

    return Value.None;
}

pub fn raiseTypeException(vm: *Vm, comptime op: []const u8, lhs: Value, rhs: Value) Error!Value {
    return vm.raiseException(.type_error, "invalid operand types for operation (\"{s}\"): {s} and {s}", .{ op, lhs.getTypeString(), rhs.getTypeString() });
}

pub inline fn raiseException(vm: *Vm, tag: Exception.Tag, comptime fmt: []const u8, args: anytype) Error!Value {
    vm.interpreter.exception = try Exception.withMessage(vm, tag, fmt, args);
    return Error.ExceptionThrown;
}
