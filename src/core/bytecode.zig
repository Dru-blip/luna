const std = @import("std");
const Span = @import("Tokenizer.zig").Token.Loc;
const Value = @import("Value.zig");

const Object = @import("../runtime/Object.zig");
const Gc = @import("Gc.zig");

pub const Inst = struct {
    op: Op,
    data: Data,

    pub const Op = enum {
        hlt,
        load_const,
        load_true,
        load_false,
        load_none,
        add,
        sub,
        mul,
        div,
        mod,

        test_lt,
        test_gt,
        test_le,
        test_ge,
        test_neq,
        test_eq,

        mov,
        branch,
        jmp,

        store_global_by_index,
        load_global_by_index,
        store_global_by_name,
        load_global_by_name,
        ret,
    };

    pub const Data = union {
        none: void,
        un: u32,
        bin: struct {
            lhs: u32,
            rhs: u32,
        },
        tri: struct {
            op1: u32,
            op2: u32,
            dst: u32,
        },
    };
};

pub const Instructions = std.ArrayList(Inst);
pub const Constants = std.ArrayList(Value);

pub const Executable = struct {
    instructions: []Inst,
    spans: []Span,
    constants: Constants.Slice,
    max_register_count: u32,
    global_variable_count: u32,

    pub fn new(gc: *Gc) !*Executable {
        const obj: *Executable = try gc.alloc(Executable);
        return obj;
    }

    pub const type_descriptor = Object.TypeDescriptor{
        .name = "Executable",
        .visit = visit,
        .finalize = finalize,
    };

    fn finalize(self: *anyopaque, gc: *Gc) void {
        const executable: *Executable = @ptrCast(@alignCast(self));
        gc.gpa.free(executable.constants);
        gc.gpa.free(executable.spans);
        gc.gpa.free(executable.instructions);
    }

    fn visit(self: *anyopaque, gc: *Gc) void {
        _ = self;
        _ = gc;
    }

    pub fn print(self: *const Executable) !void {
        var stdout_buffer: [1024]u8 = undefined;
        var stdout_writer = std.fs.File.stdout().writer(&stdout_buffer);
        var stdout = &stdout_writer.interface;

        try stdout.print("Executable\n", .{});
        try stdout.print("Max registers: {d}\n\n", .{self.max_register_count});

        for (self.instructions, 0..) |inst, idx| {
            try stdout.print("{d:0>4}: {s}", .{ idx, @tagName(inst.op) });

            try stdout.print("\n", .{});
        }

        try stdout.flush();
    }
};
