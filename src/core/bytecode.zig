const std = @import("std");
const Span = @import("Tokenizer.zig").Token.Loc;
const Value = @import("Value.zig");
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
};
