const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Object = @import("Object.zig");
const String = @import("String.zig");
const StringIterator = @import("StringIterator.zig");

const StringClass = @This();

pub fn new(gc: *Gc) !*Class {
    const sc = try Class.new(gc);
    var obj = Object.from(sc);
    obj.class = gc.interpreter.base_class;
    sc.super_class = gc.interpreter.base_class;
    return sc;
}

pub fn registerMethods(gc: *Gc, sc: *Class) !void {
    try sc.defineNativeMethod(gc, "to_num", to_number, 0, false);
    try sc.defineNativeMethod(gc, "__getattr__", getattr, 1, false);
    try sc.defineNativeMethod(gc, "__getitem__", getitem, 1, false);
    try sc.defineNativeMethod(gc, "__setitem__", setitem, 1, false);
    try sc.defineNativeMethod(gc, "__iter__", iter, 0, false);
}

fn to_number(vm: *Vm, self: *Object, _: []const Value) !Value {
    const str: *String = self.as(String);
    const number = std.fmt.parseFloat(f64, str.asSlice()) catch {
        return try vm.raiseException(vm.interpreter.value_error_class, "invalid number format", .{});
    };
    return Value.number(number);
}

fn getattr(vm: *Vm, self: *Object, args: []const Value) !Value {
    const key = args[0];
    const name = key.toObject().asString().?;
    return self.class.getField(name) orelse try vm.raiseException(
        vm.interpreter.attribute_error_class,
        "'{s}' object has no attribute '{s}'",
        .{ self.class.name.asSlice(), name.asSlice() },
    );
}

fn iter(vm: *Vm, self: *Object, _: []const Value) !Value {
    return Value.object(try StringIterator.newInstance(vm.gc, self));
}

fn getitem(vm: *Vm, self: *Object, args: []const Value) !Value {
    const string: *String = self.as(String);
    const index_value = args[0];

    if (index_value.type != .number) {
        return try vm.raiseException(
            vm.interpreter.type_error_class,
            "string index must be a number",
            .{},
        );
    }

    const index = @as(usize, @intFromFloat(index_value.data.number));
    if (index >= string.length) {
        return try vm.raiseException(
            vm.interpreter.index_error_class,
            "string index out of range",
            .{},
        );
    }

    const byte = string.asSlice()[index];
    return Value.object(try String.new(vm.gc, &[1]u8{byte}));
}

fn setitem(vm: *Vm, _: *Object, _: []const Value) !Value {
    return vm.raiseException(vm.interpreter.type_error_class, "'str' object does not support item assignment", .{});
}
