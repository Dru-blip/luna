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

fn print(vm: *Vm, _: *Object, args: []const Value) !Value {
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
                if (arg.toObject().asString()) |str| {
                    std.debug.print("{s} ", .{str.asSlice()});
                    continue;
                }
                const class = arg.toObject().class;
                const str_method_value = class.getField(try vm.interpreter.string_interner.intern("__str__"));
                if (str_method_value) |str_method_object| {
                    if (str_method_object.asObject()) |str| {
                        const s = try str.callAssumeCallable(vm, arg.toObject(), &[_]Value{});
                        //TODO: should check if s is not a string
                        std.debug.print("{s} ", .{s.toObject().asString().?.asSlice()});
                        continue;
                    }
                    std.debug.print("Object", .{});
                } else {
                    std.debug.print("Object", .{});
                }
            },
        }
    }
    std.debug.print("\n", .{});
    return Value.None;
}
