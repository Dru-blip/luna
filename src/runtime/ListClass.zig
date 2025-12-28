const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Object = @import("Object.zig");
const List = @import("List.zig");
const String = @import("String.zig");
const ListIterator = @import("ListIterator.zig");

const ListClass = @This();

pub fn new(gc: *Gc) !*Class {
    const lc = try Class.new(gc);
    return lc;
}

pub fn registerMethods(gc: *Gc, lc: *Class) !void {
    try lc.defineNativeMethod(gc, "append", append, 1, false);
    try lc.defineNativeMethod(gc, "get", get, 1, false);
    try lc.defineNativeMethod(gc, "set", set_method, 2, false);
    try lc.defineNativeMethod(gc, "insert", insert, 2, false);
    try lc.defineNativeMethod(gc, "remove", remove, 1, false);
    try lc.defineNativeMethod(gc, "pop", pop, 0, false);
    try lc.defineNativeMethod(gc, "clear", clear, 0, false);
    try lc.defineNativeMethod(gc, "size", size, 0, false);
    try lc.defineNativeMethod(gc, "contains", contains, 1, false);

    try lc.defineNativeMethod(gc, "__getattr__", getattr, 1, false);
    try lc.defineNativeMethod(gc, "__getitem__", getitem, 1, false);
    try lc.defineNativeMethod(gc, "__setitem__", setitem, 2, false);
    try lc.defineNativeMethod(gc, "__iter__", iter, 0, false);
}

fn append(_: *Vm, self: *Object, args: []const Value) !Value {
    const list: *List = self.as(List);
    const value = args[0];
    try list.append(value);
    return Value.None;
}

fn get(vm: *Vm, self: *Object, args: []const Value) !Value {
    const list: *List = self.as(List);
    const index_value = args[0];

    if (index_value.type != .number) {
        return try vm.raiseException(
            .type_error,
            "list index must be a number",
            .{},
        );
    }

    const index = @as(isize, @intFromFloat(index_value.data.number));
    return list.get(index) orelse try vm.raiseException(
        .index_error,
        "list index out of range",
        .{},
    );
}

fn set_method(vm: *Vm, self: *Object, args: []const Value) !Value {
    const list: *List = self.as(List);
    const index_value = args[0];
    const value = args[1];

    if (index_value.type != .number) {
        return try vm.raiseException(
            .type_error,
            "list index must be a number",
            .{},
        );
    }

    const index = @as(isize, @intFromFloat(index_value.data.number));
    if (!list.set(index, value)) {
        return try vm.raiseException(
            .index_error,
            "list index out of range",
            .{},
        );
    }
    return Value.None;
}

fn insert(vm: *Vm, self: *Object, args: []const Value) !Value {
    const list: *List = self.as(List);
    const index_value = args[0];
    const value = args[1];

    if (index_value.type != .number) {
        return try vm.raiseException(
            .type_error,
            "list index must be a number",
            .{},
        );
    }

    const index = @as(isize, @intFromFloat(index_value.data.number));
    try list.insert(index, value);
    return Value.None;
}

fn remove(vm: *Vm, self: *Object, args: []const Value) !Value {
    const list: *List = self.as(List);
    const index_value = args[0];

    if (index_value.type != .number) {
        return try vm.raiseException(
            .type_error,
            "list index must be a number",
            .{},
        );
    }

    const index = @as(isize, @intFromFloat(index_value.data.number));
    return list.remove(index) orelse try vm.raiseException(
        .index_error,
        "list index out of range",
        .{},
    );
}

fn pop(vm: *Vm, self: *Object, _: []const Value) !Value {
    const list: *List = self.as(List);
    return list.pop() orelse try vm.raiseException(
        .index_error,
        "pop from empty list",
        .{},
    );
}

fn clear(_: *Vm, self: *Object, _: []const Value) !Value {
    const list: *List = self.as(List);
    list.clear();
    return Value.None;
}

fn size(_: *Vm, self: *Object, _: []const Value) !Value {
    const list: *List = self.as(List);
    return Value.number(@floatFromInt(list.size()));
}

fn contains(_: *Vm, self: *Object, args: []const Value) !Value {
    const list: *List = self.as(List);
    const value = args[0];
    return Value.bool(list.contains(value));
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

fn getitem(vm: *Vm, self: *Object, args: []const Value) !Value {
    const list: *List = self.as(List);
    const index_value = args[0];

    if (index_value.type != .number) {
        return try vm.raiseException(
            .type_error,
            "list index must be a number",
            .{},
        );
    }

    const index = @as(isize, @intFromFloat(index_value.data.number));
    return list.get(index) orelse try vm.raiseException(
        .index_error,
        "list index out of range",
        .{},
    );
}

fn setitem(vm: *Vm, self: *Object, args: []const Value) !Value {
    const list: *List = self.as(List);
    const index_value = args[0];
    const value = args[1];

    if (index_value.type != .number) {
        return try vm.raiseException(
            .type_error,
            "list index must be a number",
            .{},
        );
    }

    const index = @as(isize, @intFromFloat(index_value.data.number));
    if (!list.set(index, value)) {
        return try vm.raiseException(
            .index_error,
            "list index out of range",
            .{},
        );
    }
    return Value.None;
}

fn iter(vm: *Vm, self: *Object, _: []const Value) !Value {
    return Value.object(try ListIterator.newInstance(vm.gc, self));
}
