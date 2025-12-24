const ConsoleObject = @This();

const std = @import("std");

const Vm = @import("Vm.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const Gc = @import("../core/Gc.zig");

pub fn new(gc: *Gc) !*Object {
    const obj = try gc.alloc(Object.Base);
    // try obj.defineNativeFunction(gc, "print", print, 8, true);
    return obj;
}

//TODO: implement an inspector and formatter.
fn print(_: *Vm, _: *Object, args: []Value) !Value {
    for (args) |arg| {
        switch (arg.type) {
            .int => {
                std.debug.print("{d} ", .{arg.data.int});
            },
            .float => {
                std.debug.print("{any} ", .{arg.data.float});
            },
            .bool => {
                std.debug.print("{s} ", .{if (arg.data.bool) "true" else "false"});
            },
            .none => {
                std.debug.print("none", .{});
            },
            .undefined => {
                std.debug.print("undefined", .{});
            },
            .object => {
                std.debug.print("Object", .{});
            },
        }
    }
    std.debug.print("\n", .{});
    return Value.None;
}
