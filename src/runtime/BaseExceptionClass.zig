const std = @import("std");
const Gc = @import("../core/Gc.zig");

const String = @import("String.zig");
const Object = @import("Object.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Exception = @import("Exception.zig");
const Value = @import("../core/Value.zig");

const BaseExceptionClass = @This();

pub fn MakeExceptionClass(gc: *Gc, name: *String, super: ?*Class) !*Class {
    const class: *Class = try Class.new(gc);
    class.name = name;
    class.super_class = super;
    class.constructor = constructor;

    try class.defineNativeMethod(gc, "__constructor__", constructor, 8, true);
    try class.defineNativeMethod(gc, "__getattr__", getattr, 1, false);
    try class.defineNativeMethod(gc, "__setattr__", setattr, 2, false);

    return class;
}

fn constructor(vm: *Vm, self: *Object, args: []const Value) !Value {
    const msg = args[0].toObject().toString();
    const exception = try Exception.new(vm.gc, self.as(Class), msg);
    try exception.buildTraceback(vm);
    return Value.object(Object.from(exception));
}

fn getattr(vm: *Vm, self: *Object, args: []const Value) !Value {
    const key = args[0];

    const name = key.toObject().toString();

    if (self.asClass()) |class| {
        if (class.getField(name)) |val| {
            return val;
        }
        return try vm.raiseAttributeError(
            "'{s}' object has no attribute '{s}'",
            .{ class.name.asSlice(), name.asSlice() },
        );
    }

    const instance: *Exception = self.as(Exception);
    const class: *Class = self.class;

    if (instance.getAttribute(name)) |attr| {
        return attr;
    }

    if (class.getField(name)) |value| {
        return value;
    }

    return try vm.raiseAttributeError(
        "'{s}' object has no attribute '{s}'",
        .{ class.name.asSlice(), name.asSlice() },
    );
}

fn setattr(_: *Vm, self: *Object, args: []const Value) !Value {
    const key = args[0];
    const value = args[1];

    const name = key.toObject().toString();
    const instance: *Exception = self.as(Exception);

    try instance.setAttribute(name, value);

    return Value.None;
}
