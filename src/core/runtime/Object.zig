const std = @import("std");
const Value = @import("Value.zig");
const Gc = @import("../Gc.zig");
const GcObject = Gc.GcObject;

const Object = @This();

properties: std.StringHashMap(Value),

pub fn new(gc: *Gc) !*Object {
    const obj: *Object = try gc.alloc(Object);
    // obj.properties = std.StringHashMap(Value).init(gc.gpa);
    return obj;
}

const vtable = GcObject.VTable{
    .visit = visit,
    .finalize = finalize,
};

fn finalize(self: *anyopaque, gc: *Gc) void {
    // const lu_object: *LuObject = @ptrCast(self);
    _ = self;
    _ = gc;
}

fn visit(self: *anyopaque, gc: *Gc) void {
    _ = self;
    _ = gc;
}
