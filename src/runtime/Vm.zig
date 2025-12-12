const std = @import("std");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const Interpreter = @import("Interpreter.zig");
const Executable = @import("../core/bytecode.zig").Executable;
const Inst = @import("../core/bytecode.zig").Inst;

const Vm = @This();

const Globals = struct {
    fast_slots: []Value,
    named_slots: *Object,

    pub fn deinit(self: *Globals, gpa: std.mem.Allocator) void {
        gpa.free(self.fast_slots);
    }
};

const ActivationRecord = struct {
    executable: *Executable,
    registers: []Value,
    ip: usize = 0,

    pub fn init(executable: *Executable, gpa: std.mem.Allocator) !ActivationRecord {
        return .{
            .executable = executable,
            .registers = try gpa.alloc(Value, executable.max_register_count),
        };
    }

    pub fn deinit(self: *ActivationRecord, gpa: std.mem.Allocator) void {
        gpa.free(self.registers);
    }
};

const Records = std.ArrayList(ActivationRecord);

records: Records = .empty,
rp: usize = 0,
interpreter: *Interpreter,
globals: Globals = undefined,
gpa: std.mem.Allocator,

pub fn init(gpa: std.mem.Allocator, interpreter: *Interpreter) !*Vm {
    const vm = try gpa.create(Vm);
    vm.* = .{
        .interpreter = interpreter,
        .gpa = gpa,
    };
    return vm;
}

pub fn deinit(vm: *Vm) void {
    vm.globals.deinit(vm.gpa);
    vm.records.deinit(vm.gpa);
    vm.gpa.destroy(vm);
}

pub fn runExecutable(vm: *Vm, executable: *Executable) !Value {
    var record = try ActivationRecord.init(executable, vm.gpa);
    vm.globals.fast_slots = try vm.gpa.alloc(Value, executable.global_variable_count);
    defer record.deinit(vm.gpa);
    return vm.runRecord(&record, false);
}

pub fn runRecord(vm: *Vm, record: *ActivationRecord, as_callback: bool) !Value {
    _ = as_callback;
    var registers = record.registers;
    var globals = vm.globals;
    const instructions = record.executable.instructions;
    const constants = record.executable.constants;
    var instruction: *Inst = undefined;

    start: while (true) {
        instruction = &instructions[record.ip];
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
            .mov => {
                registers[data.bin.rhs] = registers[data.bin.lhs];
                continue :start;
            },
            .add => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.int(lhs.toInt() + rhs.toInt());
                }
                continue :start;
            },
            .sub => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.int(lhs.toInt() - rhs.toInt());
                }
                continue :start;
            },
            .mul => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.int(lhs.toInt() * rhs.toInt());
                }
                continue :start;
            },
            .div => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    if (rhs.toInt() == 0) {
                        //TODO: throw error for division by zero
                    }
                    registers[data.tri.dst] = Value.int(@divFloor(lhs.toInt(), rhs.toInt()));
                }
                continue :start;
            },
            .mod => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    if (rhs.toInt() == 0) {
                        //TODO: throw error for division by zero
                    }
                    registers[data.tri.dst] = Value.int(@mod(lhs.toInt(), rhs.toInt()));
                }
                continue :start;
            },
            .test_lt => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() < rhs.toInt());
                }
                continue :start;
            },
            .test_le => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() <= rhs.toInt());
                }
                continue :start;
            },
            .test_gt => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() > rhs.toInt());
                }
                continue :start;
            },
            .test_ge => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() >= rhs.toInt());
                }
                continue :start;
            },
            .test_eq => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() == rhs.toInt());
                }
                continue :start;
            },
            .test_neq => {
                const lhs = registers[data.tri.op1];
                const rhs = registers[data.tri.op2];
                if (lhs.type != rhs.type) {
                    //TODO: throw error for type mismatch
                }
                if (lhs.isInt() and rhs.isInt()) {
                    registers[data.tri.dst] = Value.bool(lhs.toInt() != rhs.toInt());
                }
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
            .ret => {
                //TODO: destroy activation record,
                return registers[data.un];
            },
            .hlt => {
                return Value.none();
            },
        }
    }

    return Value.none();
}
