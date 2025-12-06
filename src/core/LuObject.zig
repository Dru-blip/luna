const std = @import("std");
const Value = @import("Value.zig");
const Gc = @import("Gc.zig");
const GcObject = Gc.GcObject;

const LuObject = @This();

meta: GcObject,
properties: std.StringHashMap(Value),

pub fn new(gc: *Gc) !*LuObject {
    const obj: *LuObject = try gc.alloc(LuObject);
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
    // const lu_object: *LuObject = @ptrCast(self);
    _ = self;
    _ = gc;
}

fn visit(self: *anyopaque, gc: *Gc) void {
    _ = self;
    _ = gc;
}
