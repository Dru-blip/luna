const std = @import("std");

const Vm = @import("Vm.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const Gc = @import("../core/Gc.zig");
const ObjectSet = @import("ObjectSet.zig");

const NativeFunction = @This();

pub const Function = *const fn (*Vm, *Object, []Value) Value;

arity: u8,
function: Function,
isVariadic: bool = false,

pub fn new(gc: *Gc, func: Function, arity: u8, isVariadic: bool) !*Object {
    var obj = try gc.alloc(NativeFunction);
    var s: *NativeFunction = obj.as(NativeFunction);
    s.function = func;
    s.arity = arity;
    s.isVariadic = isVariadic;
    return obj;
}

pub const type_descriptor: Object.TypeDescriptor = .{
    .name = "NativeFunction",
    .visit = visit,
    .finalize = finalize,
};

fn finalize(self: *Object, gc: *Gc) void {
    Object.Base.finalize(self, gc);
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
}
