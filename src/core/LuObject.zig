const std = @import("std");
const Value = @import("Value.zig");
const Gc = @import("Gc.zig");

const LuObject = @This();

meta: Gc.GcObject,
properties: std.StringHashMap(Value),

pub fn new(gc: *Gc) !*LuObject {
    const obj: *LuObject = try gc.alloc(LuObject);
    const meta: Gc.GcObject = Gc.GcObject.from(obj);
    obj.meta = meta;
    obj.properties = std.StringHashMap(Value).init(gc.gpa);
    return obj;
}
