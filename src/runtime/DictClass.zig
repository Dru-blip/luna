const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Object = @import("Object.zig");
const Dict = @import("Dict.zig");
const String = @import("String.zig");

const DictClass = @This();

pub fn new(gc: *Gc) !*Class {
    const dc = try Class.new(gc);
    return dc;
}

pub fn registerMethods(gc: *Gc, dc: *Class) !void {
    try dc.defineNativeMethod(gc, "set", set, 2, false);
    try dc.defineNativeMethod(gc, "get", get, 1, false);
    try dc.defineNativeMethod(gc, "remove", remove, 1, false);
    try dc.defineNativeMethod(gc, "contains", contains, 1, false);
    try dc.defineNativeMethod(gc, "clear", clear, 0, false);
    try dc.defineNativeMethod(gc, "size", size, 0, false);

    try dc.defineNativeMethod(gc, "__getattr__", getattr, 1, false);
    try dc.defineNativeMethod(gc, "__setattr__", setattr, 2, false);
    try dc.defineNativeMethod(gc, "__getitem__", getitem, 1, false);
    try dc.defineNativeMethod(gc, "__setitem__", setitem, 2, false);
}

fn set(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    const value = args[1];
    try dict.set(vm, key, value);
    return Value.None;
}

fn get(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    return try dict.get(vm, key) orelse Value.None;
}

fn remove(_: *Vm, _: *Object, _: []const Value) !Value {
    // const dict = self.as(Dict);
    // const key = args[0];
    return Value.bool(true);
}

fn contains(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict = self.as(Dict);
    const key = args[0];
    return Value.bool(try dict.contains(vm, key));
}

fn clear(_: *Vm, _: *Object, _: []const Value) !Value {
    // const dict = self.as(Dict);
    // dict.clear();
    return Value.None;
}

fn size(_: *Vm, self: *Object, _: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    return Value.number(@floatFromInt(dict.size()));
}

fn getattr(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    if (try dict.get(vm, key)) |value| {
        return value;
    }
    const name = key.toObject().asString().?;
    return self.class.getField(name) orelse try vm.raiseException(
        .attribute_error,
        "'{s}' object has no attribute '{s}'",
        .{ self.class.name.asSlice(), name.asSlice() },
    );
}

fn setattr(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    const value = args[1];
    try dict.set(vm, key, value);
    return Value.None;
}

fn getitem(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    const res = try dict.get(vm, key);
    return res orelse Value.None;
}

fn setitem(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    const value = args[1];
    try dict.set(vm, key, value);
    return Value.None;
}
