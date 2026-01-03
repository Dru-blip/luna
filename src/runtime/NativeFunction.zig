const std = @import("std");

const Vm = @import("Vm.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const Gc = @import("../core/Gc.zig");
const ObjectSet = @import("ObjectSet.zig");
const Interpreter = @import("Interpreter.zig");
const String = @import("String.zig");
const Class = @import("Class.zig");

const NativeFunction = @This();

pub const Function = *const fn (*Vm, *Object, []const Value) Interpreter.Error!Value;

name: *String,
arity: u8,
function: Function,
isVariadic: bool = false,
home_class: *Class = undefined,

pub fn new(gc: *Gc, home_class: *Class, func: Function, name: *String, arity: u8, isVariadic: bool) !*Object {
    var obj = try gc.alloc(NativeFunction);
    obj.class = gc.interpreter.base_class;
    var s: *NativeFunction = obj.as(NativeFunction);
    s.name = name;
    s.function = func;
    s.arity = arity;
    s.isVariadic = isVariadic;
    s.home_class = home_class;
    return obj;
}

pub const gc_hooks: Object.GcHooks = .{
    .name = "NativeFunction",
    .visit = visit,
    .finalize = Object.Base.finalize,
};

// fn finalize(self: *Object, gc: *Gc) void {
//     Object.Base.finalize(self, gc);
// }

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const native: *NativeFunction = self.as(NativeFunction);
    const class_obj = Object.from(native.home_class);
    try class_obj.gc_hooks.visit(class_obj, live_objects);
}
