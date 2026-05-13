const std = @import("std");
const Vm = @import("../Vm.zig");
const Value = @import("../../core/Value.zig");
const Object = @import("../Object.zig");
const Gc = @import("../../core/Gc.zig");
const Class = @import("../Class.zig");
const String = @import("../String.zig");
const List = @import("../List.zig");
const Dict = @import("../Dict.zig");
const Interpreter = @import("../Interpreter.zig");

pub fn new(gc: *Gc) !*Class {
    //TODO: should return Module object instead of Class object.
    const class = try Class.new(gc);
    class.name = gc.interpreter.common_names.cli;
    Object.from(class).class = class;
    class.super_class = gc.interpreter.base_class;

    try class.defineNativeMethod(gc, "args", getArgs, 0, false);
    try class.defineNativeMethod(gc, "exit", exitProcess, 1, false);
    try class.defineNativeMethod(gc, "getenv", getEnv, 1, false);
    try class.defineNativeMethod(gc, "cwd", getCwd, 0, false);

    return class;
}

fn getArgs(vm: *Vm, _: *Object, _: []const Value) !Value {
    const args_list: *List = (try List.new(vm.gc)).as(List);

    var args_iter = try std.process.argsWithAllocator(vm.gpa);
    while (args_iter.next()) |arg| {
        const arg_str = try String.new(vm.gc, arg);
        try args_list.append(Value.object(arg_str));
    }

    return Value.object(Object.from(args_list));
}

fn exitProcess(vm: *Vm, _: *Object, args: []const Value) !Value {
    if (args.len == 0) {
        std.process.exit(0);
    }

    const exit_code = args[0];
    if (!exit_code.isNumber()) {
        return vm.raiseTypeError("exit() expects a number", .{});
    }

    const code = @as(u8, @intCast(@as(i64, @intFromFloat(exit_code.asNumber()))));
    std.process.exit(code);
}

fn getEnv(vm: *Vm, _: *Object, args: []const Value) !Value {
    if (args.len == 0) {
        return vm.raiseTypeError("getenv() requires 1 argument", .{});
    }

    const key_value = args[0];
    if (!key_value.isString()) {
        return vm.raiseTypeError("getenv() expects a string", .{});
    }

    const key = key_value.toObject().toString();
    const env_value = std.process.getEnvVarOwned(vm.gpa, key.asSlice()) catch {
        return Value.None;
    };
    defer vm.gpa.free(env_value);

    return Value.object(try String.new(vm.gc, env_value));
}

fn getCwd(vm: *Vm, _: *Object, _: []const Value) !Value {
    const cwd = std.process.getCwdAlloc(vm.gpa) catch "";
    defer vm.gpa.free(cwd);

    return Value.object(try String.new(vm.gc, cwd));
}
