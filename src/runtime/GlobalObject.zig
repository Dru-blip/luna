const std = @import("std");
const Object = @import("Object.zig");
const Gc = @import("../core/Gc.zig");
const ConsoleObject = @import("ConsoleObject.zig");
const Value = @import("../core/Value.zig");
const Vm = @import("Vm.zig");
const Class = @import("Class.zig");

pub fn new(gc: *Gc) !*Class {
    const class = try Class.new(gc);

    try class.defineNativeMethod(gc, "print", print, 10, true);

    return class;
}

fn print(_: *Vm, _: *Object, args: []Value) !Value {
    for (args) |arg| {
        switch (arg.type) {
            .number => {
                std.debug.print("{any} ", .{arg.data.number});
            },
            .bool => {
                std.debug.print("{s} ", .{if (arg.data.bool) "true" else "false"});
            },
            .none => {
                std.debug.print("none", .{});
            },
            .object => {
                std.debug.print("Object", .{});
            },
        }
    }
    std.debug.print("\n", .{});
    return Value.None;
}
