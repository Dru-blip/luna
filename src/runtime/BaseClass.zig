const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Object = @import("Object.zig");
const String = @import("String.zig");
const Instance = @import("Instance.zig");

const BaseClass = @This();

pub fn registerMethods(gc: *Gc, class: *Class) !void {
    try class.defineNativeMethod(gc, "__getattr__", getattr, 1, false);
    try class.defineNativeMethod(gc, "__setattr__", setattr, 2, false);
}

fn getattr(vm: *Vm, self: *Object, args: []const Value) !Value {
    const key = args[0];

    const name = key.toObject().toString();
    const instance: *Instance = self.as(Instance);
    const class: *Class = instance.class;

    if (instance.getAttribute(name)) |attr| {
        return attr;
    }

    if (class.getField(name)) |value| {
        return value;
    }

    var current_class: ?*Class = self.class;
    while (current_class) |cls| {
        if (cls.getField(name)) |value| {
            return value;
        }
        current_class = cls.super_class;
    }

    return try vm.raiseException(
        .attribute_error,
        "'{s}' object has no attribute '{s}'",
        .{ class.name.asSlice(), name.asSlice() },
    );
}

fn setattr(_: *Vm, self: *Object, args: []const Value) !Value {
    const key = args[0];
    const value = args[1];

    const name = key.toObject().toString();
    const instance: *Instance = self.as(Instance);

    try instance.setAttribute(name, value);

    return Value.None;
}
