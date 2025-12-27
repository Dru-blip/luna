const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");

const List = @This();

items: std.ArrayList(Value),
gpa: std.mem.Allocator,

pub const type_descriptor = Object.TypeDescriptor{
    .name = "List",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc) !*Object {
    const obj = try gc.alloc(List);
    obj.class = gc.interpreter.list_class;
    var list: *List = obj.as(List);
    list.items = .empty;
    list.gpa = gc.gpa;
    return obj;
}

pub fn append(list: *List, value: Value) !void {
    try list.items.append(list.gpa, value);
}

inline fn normalizeIndex(list: *List, index: isize) ?usize {
    const len = @as(isize, @intCast(list.items.items.len));
    var normalized: isize = index;

    if (normalized < 0) {
        normalized += len;
    }

    if (normalized < 0 or normalized >= len) {
        return null;
    }

    return @intCast(normalized);
}

pub fn get(list: *List, index: isize) ?Value {
    const norm_index = list.normalizeIndex(index) orelse return null;
    return list.items.items[norm_index];
}

pub fn set(list: *List, index: isize, value: Value) bool {
    const norm_index = list.normalizeIndex(index) orelse return false;
    list.items.items[norm_index] = value;
    return true;
}

pub fn insert(list: *List, index: isize, value: Value) !void {
    const len = @as(isize, @intCast(list.items.items.len));
    var norm_index: isize = index;

    if (norm_index < 0) {
        norm_index += len;
        if (norm_index < 0) {
            norm_index = 0;
        }
    } else if (norm_index > len) {
        norm_index = len;
    }

    try list.items.insert(list.gpa, @intCast(norm_index), value);
}

pub fn remove(list: *List, index: isize) ?Value {
    const norm_index = list.normalizeIndex(index) orelse return null;
    return list.items.orderedRemove(norm_index);
}

pub fn pop(list: *List) ?Value {
    return list.items.pop();
}

pub fn clear(list: *List) void {
    list.items.clearAndFree(list.gpa);
}

pub fn size(list: *List) usize {
    return list.items.items.len;
}

pub fn contains(list: *List, value: Value) bool {
    for (list.items.items) |item| {
        if (item.eql(value)) {
            return true;
        }
    }
    return false;
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const list: *List = self.as(List);

    for (list.items.items) |item| {
        if (item.asObject()) |obj| {
            if (!live_objects.contains(obj)) {
                try obj.type_descriptor.visit(obj, live_objects);
            }
        }
    }
}

fn finalize(self: *Object, gc: *Gc) void {
    const list: *List = self.as(List);
    list.items.deinit(list.gpa);
    Object.Base.finalize(self, gc);
}
