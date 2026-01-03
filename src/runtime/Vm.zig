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
const default_record_capacity = 2048;

records: Records = .empty,
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
        .records = try Records.initCapacity(gpa, default_record_capacity),
        .register_pool = register_pool,
    };
    return vm;
}

pub fn deinit(vm: *Vm) void {
    vm.records.deinit(vm.gpa);
    vm.register_pool.deinit(vm.gpa);
    vm.gpa.destroy(vm);
}

pub fn runExecutable(vm: *Vm, executable: *Executable) Error!Value {
    const record = try ActivationRecord.init(executable, &vm.register_pool);
    vm.records.appendAssumeCapacity(record);
    return try vm.runRecord(&vm.records.items[vm.records.items.len - 1], false);
}

pub fn runRecord(vm: *Vm, r: *ActivationRecord, as_callback: bool) Error!Value {
    var record = r;
    var registers = record.registers;
    var globals = vm.getGlobalSlots();
    var instructions = record.executable.instructions;
    var identifiers = record.executable.identifiers;
    var constants = record.executable.constants;
    var extra = record.executable.extra;
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
            .load_undefined => {
                registers[data.un] = Value.Undefined;
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
            .negate => {
                const value = registers[data.bin.lhs];
                if (value.isNumeric()) {
                    registers[data.bin.rhs] = Value.number(-value.asNumber());
                    continue :start;
                }
                return vm.raiseException(.type_error, "invalid operand type for unary operator ('-') : '{s}'", .{value.getTypeString()});
            },
            .not => {
                const value = registers[data.bin.lhs];
                registers[data.bin.rhs] = Value.bool(!value.isTruthy());
                continue :start;
            },
            .un_plus => {
                //TODO: should implement.
                continue :start;
            },
            .add => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];

                if (lhs.isNumeric() and rhs.isNumeric()) {
                    registers[data.tri.dst] = Value.number(lhs.asNumber() + rhs.asNumber());
                    continue :start;
                }

                if (lhs.isString() and rhs.isString()) {
                    const a = lhs.toObject().toString();
                    const b = rhs.toObject().toString();
                    registers[data.tri.dst] = Value.object(try a.concat(vm.gc, b));
                    continue :start;
                }

                if (lhs.isString() or rhs.isString()) {
                    const self_str = if (lhs.isString())
                        lhs.toObject().toString()
                    else
                        rhs.toObject().toString();

                    const other_value = if (lhs.isString()) rhs else lhs;

                    var other_str: *String = undefined;
                    if (other_value.isObject()) {
                        const s = try vm.invokeSpecialMethod(other_value, vm.interpreter.common_names.__str__, &[_]Value{});
                        if (s.isString()) {
                            other_str = s.toObject().toString();
                        }
                        _ = try vm.raiseException(.type_error, "__str__ method returned non-string value", .{});
                    } else {
                        other_str = (try String.new(vm.gc, other_value.toString())).asString().?;
                    }
                    registers[data.tri.dst] = Value.object(try self_str.concat(vm.gc, other_str));
                    continue :start;
                }

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__add__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__radd__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__sub__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__rsub__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__mul__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__rmul__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__div__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__rdiv__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__mod__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__rmod__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isString() and rhs.isString()) {
                    const ls = lhs.toObject().toString();
                    const rs = rhs.toObject().toString();
                    registers[data.tri.dst] = Value.bool(ls.cmp(rs, .l));
                    continue :start;
                }

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__lt__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__gt__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isString() and rhs.isString()) {
                    const ls = lhs.toObject().toString();
                    const rs = rhs.toObject().toString();
                    registers[data.tri.dst] = Value.bool(ls.cmp(rs, .le));
                    continue :start;
                }

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__le__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__ge__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isString() and rhs.isString()) {
                    const ls = lhs.toObject().toString();
                    const rs = rhs.toObject().toString();
                    registers[data.tri.dst] = Value.bool(ls.cmp(rs, .g));
                    continue :start;
                }

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__gt__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__lt__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isString() and rhs.isString()) {
                    const ls = lhs.toObject().toString();
                    const rs = rhs.toObject().toString();
                    registers[data.tri.dst] = Value.bool(ls.cmp(rs, .ge));
                    continue :start;
                }

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__ge__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__le__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__eq__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__eq__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
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

                if (lhs.isObject()) {
                    const class = lhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__ne__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, lhs.toObject(), &[_]Value{rhs});
                        }
                        continue :start;
                    }
                }

                if (rhs.isObject()) {
                    const class = rhs.toObject().class;
                    if (class.getField(vm.interpreter.common_names.__ne__)) |method| {
                        if (method.asObject()) |obj| {
                            registers[data.tri.dst] = try obj.callAssumeCallable(vm, rhs.toObject(), &[_]Value{lhs});
                        }
                        continue :start;
                    }
                }

                registers[data.tri.dst] = Value.bool(!lhs.eql(rhs));
                continue :start;
            },
            .build_function => {
                const func_obj = try Function.withExecutable(vm.gc, constants[data.bin.lhs].toObject().as(Executable));
                const function: *Function = func_obj.as(Function);
                function.module_env = vm.interpreter.getRunningModule();
                registers[data.bin.rhs] = Value.object(func_obj);
                continue :start;
            },
            .build_class => {
                const class_name = identifiers[data.bin.lhs];
                const class = try Class.new(vm.gc);
                class.super_class = vm.interpreter.base_class;
                class.name = class_name.toObject().toString();
                Object.from(class).class = vm.interpreter.base_class;
                registers[data.bin.rhs] = Value.object(Object.from(class));
                continue :start;
            },
            .set_super_class => {
                const sub_class_value = registers[data.bin.rhs];
                const super_class_value = registers[data.bin.lhs];

                if (super_class_value.asClass()) |super_class| {
                    const sub_class: *Class = sub_class_value.toObject().as(Class);
                    sub_class.super_class = super_class;
                    continue :start;
                }

                return vm.raiseException(.type_error, "Expected a class", .{});
            },
            .get_super_class => {
                //ERROR: this is useless.
                const super_class = record.function.?.home_class.?.super_class.?;
                registers[data.un] = Value.object(Object.from(super_class));
                continue :start;
            },
            .add_class_method => {
                const class: *Class = registers[data.bin.rhs].toObject().as(Class);
                const method: *Function = registers[data.bin.lhs].toObject().as(Function);
                method.home_class = class;
                try class.addMethod(method.exe.name, registers[data.bin.lhs]);
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
                try dict.set(vm, key, value);
                continue :start;
            },
            .build_list => {
                const list = try List.new(vm.gc);
                registers[data.un] = Value.object(list);
                continue :start;
            },
            .append_list_item => {
                const list: *List = registers[data.bin.rhs].toObject().as(List);
                const item = registers[data.bin.lhs];
                try list.append(item);
                continue :start;
            },
            .get_item => {
                const target = registers[data.tri.op1];
                const index = registers[data.tri.op2];

                var args: [1]Value = undefined;
                args[0] = index;

                registers[data.tri.dst] = try vm.invokeSpecialMethod(
                    target,
                    vm.interpreter.common_names.__getitem__,
                    &args,
                );
                continue :start;
            },
            .set_item => {
                const target = registers[data.tri.op1];
                const index = registers[data.tri.op2];

                var args: [2]Value = undefined;
                args[0] = index;
                args[1] = registers[data.tri.dst];

                _ = try vm.invokeSpecialMethod(
                    target,
                    vm.interpreter.common_names.__setitem__,
                    &args,
                );

                continue :start;
            },
            .get_attribute => {
                const target = registers[data.tri.op1];
                const index = constants[data.tri.op2];

                var args: [1]Value = undefined;
                args[0] = index;

                registers[data.tri.dst] = try vm.invokeSpecialMethod(
                    target,
                    vm.interpreter.common_names.__getattr__,
                    &args,
                );
                continue :start;
            },
            .set_attribute => {
                const target = registers[data.tri.dst];
                const index = registers[data.tri.op2];

                var args: [2]Value = undefined;
                args[1] = index;
                args[0] = registers[data.tri.op1];

                _ = try vm.invokeSpecialMethod(
                    target,
                    vm.interpreter.common_names.__setattr__,
                    &args,
                );

                continue :start;
            },
            .get_iter => {
                const iterable_value = registers[data.bin.lhs];
                registers[data.bin.rhs] = try vm.invokeSpecialMethod(
                    iterable_value,
                    vm.interpreter.common_names.__iter__,
                    &[_]Value{},
                );
                continue :start;
            },
            .iter_next => {
                const iterator_value = registers[data.bin.lhs];
                registers[data.bin.rhs] = try vm.invokeSpecialMethod(
                    iterator_value,
                    vm.interpreter.common_names.__next__,
                    &[_]Value{},
                );
                continue :start;
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
            .super_call => {
                const super_class = record.function.?.home_class.?.super_class.?;
                const method_name = identifiers[data.super_call.method].toObject().toString();
                const method_value = super_class.getField(method_name);
                const this_value = registers[0];

                if (method_value) |meth_val| {
                    if (meth_val.asObject()) |meth_obj| {
                        const args_data = extra[data.super_call.arg_offset .. data.super_call.arg_offset + data.super_call.argc];

                        var args: [8]Value = undefined;
                        for (args_data, 0..) |arg, i| {
                            args[i] = record.registers[arg];
                        }

                        _ = try meth_obj.callAssumeCallable(vm, this_value.toObject(), args[0..data.super_call.argc]);
                        continue :start;
                    }
                }
                return try vm.raiseException(
                    .attribute_error,
                    "'{s}' has no method '{s}'",
                    .{ super_class.name.asSlice(), method_name.asSlice() },
                );
            },
            .call => {
                const callee = registers[data.call.callee];
                if (!callee.isObject()) {
                    return vm.raiseException(.type_error, "'{s}' is not callable", .{callee.getTypeString()});
                }
                const callee_obj = callee.toObject();

                if (!callee_obj.isFunction()) {
                    return vm.raiseException(.type_error, "'{s}' object is not callable", .{callee_obj.getClassName()});
                }

                const this_value = registers[data.call.this];

                const args_data = extra[data.call.arg_offset .. data.call.arg_offset + data.call.argc];

                if (callee_obj.asNativeFunction()) |native_function| {
                    _ = try vm.checkArity(NativeFunction, native_function.name, native_function.arity, @intCast(data.call.argc), native_function.isVariadic);

                    var args: [8]Value = undefined;

                    for (args_data, 0..) |arg, i| {
                        args[i] = record.registers[arg];
                    }

                    registers[data.call.ret] = try native_function.function(vm, this_value.toObject(), args[0..data.call.argc]);
                    continue :start;
                }

                if (callee_obj.asClass()) |class| {
                    var args: [8]Value = undefined;
                    for (args_data, 0..) |arg, i| {
                        args[i] = record.registers[arg];
                    }
                    registers[data.call.ret] = Value.object(try class.newInstance(vm.gc));
                    if (class.getField(vm.interpreter.common_names.__init__)) |constructor| {
                        _ = try constructor.toObject().callAssumeCallable(vm, registers[data.call.ret].toObject(), args[0..data.call.argc]);
                    }
                    continue :start;
                }

                const function: *Function = callee_obj.as(Function);

                _ = try vm.checkArity(Function, function.exe.name, function.arity, @intCast(data.call.argc), false);

                const caller_record = vm.records.getLast();
                vm.records.appendAssumeCapacity(try ActivationRecord.withFunction(&vm.register_pool, function));
                record = &vm.records.items[vm.records.items.len - 1];
                record.caller_return_reg = data.call.ret;

                record.registers[0] = this_value;
                for (args_data, 0..) |arg, i| {
                    record.registers[i + 1] = caller_record.registers[arg];
                }

                registers = record.registers;
                instructions = record.executable.instructions;
                constants = record.executable.constants;
                identifiers = record.executable.identifiers;
                extra = record.executable.extra;
                globals = vm.getGlobalSlots();
                continue :start;
            },
            .ret => {
                var callee = vm.records.pop().?;
                if (vm.records.items.len == 0 or !vm.interpreter.isMainModule() or as_callback) {
                    return registers[data.un];
                }

                record = &vm.records.items[vm.records.items.len - 1];

                registers = record.registers;
                instructions = record.executable.instructions;
                constants = record.executable.constants;
                identifiers = record.executable.identifiers;
                extra = record.executable.extra;
                globals = vm.getGlobalSlots();
                record.registers[callee.caller_return_reg] = callee.registers[data.un];
                callee.deinit(&vm.register_pool);
                continue :start;
            },
            .ret_none => {
                var callee = vm.records.pop().?;
                if (vm.records.items.len == 0 or !vm.interpreter.isMainModule() or as_callback) {
                    return Value.None;
                }
                record = &vm.records.items[vm.records.items.len - 1];

                registers = record.registers;
                instructions = record.executable.instructions;
                constants = record.executable.constants;
                identifiers = record.executable.identifiers;
                extra = record.executable.extra;
                globals = vm.getGlobalSlots();
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
        return try vm.raiseException(
            .type_error,
            "'{s}' expects {d} arguments but got {d}",
            .{ name.asSlice(), expected, actual },
        );
    }

    return Value.None;
}

fn invokeSpecialMethod(
    vm: *Vm,
    target: Value,
    method_name: *String,
    args: []const Value,
) Error!Value {
    //TODO: refactor
    if (!target.isObject()) {
        return vm.raiseException(
            .type_error,
            "invalid type '{s}' for operation '{s}'",
            .{ target.getTypeString(), method_name.asSlice() },
        );
    }

    const cls = target.toObject().class;
    const method = cls.getField(method_name) orelse {
        return vm.raiseException(
            .type_error,
            "'{s}' object does not support special method '{s}'",
            .{ target.getTypeString(), method_name.asSlice() },
        );
    };

    const callable = method.asObject() orelse {
        return vm.raiseException(
            .type_error,
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

pub fn raiseTypeException(vm: *Vm, comptime op: []const u8, lhs: Value, rhs: Value) Error!Value {
    return vm.raiseException(.type_error, "invalid operand types for operation (\"{s}\"): {s} and {s}", .{ op, lhs.getTypeString(), rhs.getTypeString() });
}

pub inline fn raiseException(vm: *Vm, tag: Exception.Tag, comptime fmt: []const u8, args: anytype) Error!Value {
    vm.interpreter.exception = try Exception.withMessage(vm, tag, fmt, args);
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
