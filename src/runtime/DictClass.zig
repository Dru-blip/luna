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

fn set(_: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    const value = args[1];
    try dict.set(key, value);
    return Value.None;
}

fn get(_: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    return dict.get(key) orelse Value.None;
}

fn remove(_: *Vm, self: *Object, args: []const Value) !Value {
    const dict = self.as(Dict);
    const key = args[0];
    return Value.bool(dict.remove(key));
}

fn contains(_: *Vm, self: *Object, args: []const Value) !Value {
    const dict = self.as(Dict);
    const key = args[0];
    return Value.bool(dict.contains(key));
}

fn clear(_: *Vm, self: *Object, _: []const Value) !Value {
    const dict = self.as(Dict);
    dict.clear();
    return Value.None;
}

fn size(_: *Vm, self: *Object, _: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    return Value.number(@floatFromInt(dict.size()));
}

fn getattr(_: *Vm, self: *Object, args: []const Value) !Value {
    const dict = self.as(Dict);
    const key = args[0];
    return dict.get(key) orelse Value.None;
}

fn setattr(_: *Vm, self: *Object, args: []const Value) !Value {
    const dict = self.as(Dict);
    const key = args[0];
    const value = args[1];
    try dict.set(key, value);
    return Value.None;
}

fn getitem(_: *Vm, self: *Object, args: []const Value) !Value {
    const dict: *Dict = self.as(Dict);
    const key = args[0];
    const res = dict.get(key);
    return res orelse Value.None;
}

fn setitem(_: *Vm, self: *Object, args: []const Value) !Value {
    const dict = self.as(Dict);
    const key = args[0];
    const value = args[1];
    try dict.set(key, value);
    return Value.None;
}
