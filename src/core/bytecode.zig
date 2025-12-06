const std = @import("std");
const Span = @import("Tokenizer.zig").Token.Loc;
const Value = @import("Value.zig");

const Gc = @import("Gc.zig");
const GcObject = @import("Gc.zig").GcObject;

pub const Inst = struct {
    op: Op,
    data: Data,

    pub const Op = enum {
        hlt,
        load_const,
        add,
        sub,
        mul,
        div,
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
            arg1: u32,
            arg2: u32,
            arg3: u32,
        },
    };
};

pub const Instructions = std.ArrayList(Inst);
pub const Constants = std.ArrayList(Value);

pub const Executable = struct {
    meta: GcObject,
    instructions: []Inst,
    spans: []Span,
    constants: Constants.Slice,
    max_register_count: u32,

    pub fn new(gc: *Gc) !*Executable {
        const obj: *Executable = try gc.alloc(Executable);
        const meta: GcObject = GcObject.from(obj, &vtable);
        obj.meta = meta;
        // obj.properties = std.StringHashMap(Value).init(gc.gpa);
        return obj;
    }

    const vtable = GcObject.VTable{
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
};
