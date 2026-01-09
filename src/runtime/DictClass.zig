const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Object = @import("Object.zig");
const Dict = @import("Dict.zig");
const String = @import("String.zig");
const DictIterator = @import("DictIterator.zig");
const DictKeyIterator = @import("dict_iterator.zig").DictKeyIterator;
const DictValueIterator = @import("dict_iterator.zig").DictValueIterator;

const DictClass = @This();

pub fn new(gc: *Gc) !*Class {
    const dc = try Class.new(gc);
    dc.constructor = constructor;
    return dc;
}

fn constructor(vm: *Vm, _: *Object, _: []const Value) !Value {
    const dict = try Dict.new(vm.gc);
    return Value.object(dict);
}

pub fn registerMethods(gc: *Gc, dc: *Class) !void {
    try dc.defineNativeMethod(gc, "set", set, 2, false);
    try dc.defineNativeMethod(gc, "get", get, 1, false);
    try dc.defineNativeMethod(gc, "remove", remove, 1, false);
    try dc.defineNativeMethod(gc, "contains", contains, 1, false);
    try dc.defineNativeMethod(gc, "clear", clear, 0, false);
    try dc.defineNativeMethod(gc, "keys", keys, 0, false);
    try dc.defineNativeMethod(gc, "values", values, 0, false);

    try dc.defineNativeMethod(gc, "__constructor__", constructor, 8, true);
    try dc.defineNativeMethod(gc, "__getattr__", getattr, 1, false);
    try dc.defineNativeMethod(gc, "__setattr__", setattr, 2, false);
    try dc.defineNativeMethod(gc, "__getitem__", getitem, 1, false);
    try dc.defineNativeMethod(gc, "__setitem__", setitem, 2, false);
    try dc.defineNativeMethod(gc, "__iter__", iter, 0, false);
    try dc.defineNativeMethod(gc, "__len__", size, 0, false);
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

fn remove(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    //TODO: should raise key error,if not found.
    return try dict.remove(vm, key) orelse Value.None;
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

fn keys(vm: *Vm, self: *Object, _: []const Value) !Value {
    return Value.object(try DictKeyIterator.newInstance(vm.gc, self));
}

fn values(vm: *Vm, self: *Object, _: []const Value) !Value {
    return Value.object(try DictValueIterator.newInstance(vm.gc, self));
}

fn getattr(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    if (try dict.get(vm, key)) |value| {
        return value;
    }
    const name = key.toObject().asString().?;
    return self.class.getField(name) orelse try vm.raiseAttributeError(
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
    //TODO: should raise key error,if not found.
    return res orelse Value.None;
}

fn setitem(vm: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    const value = args[1];
    try dict.set(vm, key, value);
    return Value.None;
}

fn iter(vm: *Vm, self: *Object, _: []const Value) !Value {
    return Value.object(try DictIterator.newInstance(vm.gc, self));
}
