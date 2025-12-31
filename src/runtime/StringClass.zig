const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Object = @import("Object.zig");
const String = @import("String.zig");

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
}

fn to_number(vm: *Vm, self: *Object, _: []const Value) !Value {
    const str: *String = self.as(String);
    const number = std.fmt.parseFloat(f64, str.asSlice()) catch {
        return try vm.raiseException(.value_error, "invalid number format", .{});
    };
    return Value.number(number);
}

fn getattr(vm: *Vm, self: *Object, args: []const Value) !Value {
    const key = args[0];
    const name = key.toObject().asString().?;
    return self.class.getField(name) orelse try vm.raiseException(
        .attribute_error,
        "'{s}' object has no attribute '{s}'",
        .{ self.class.name.asSlice(), name.asSlice() },
    );
}
