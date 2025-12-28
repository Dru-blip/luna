const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const List = @import("List.zig");
const String = @import("String.zig");

const ListIterator = @This();

list: *Object,
index: usize,

pub const type_descriptor = Object.TypeDescriptor{
    .name = "ListIterator",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc) !*Class {
    const ic = try Class.new(gc);
    //TODO: Should switch to base iterator class
    ic.super_class = gc.interpreter.base_class;
    return ic;
}

pub fn newInstance(gc: *Gc, list_obj: *Object) !*Object {
    const obj = try gc.alloc(ListIterator);
    obj.class = gc.interpreter.list_iterator_class;
    var iterator: *ListIterator = obj.as(ListIterator);
    iterator.list = list_obj;
    iterator.index = 0;
    return obj;
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const iterator: *ListIterator = self.as(ListIterator);

    if (!live_objects.contains(iterator.list)) {
        try iterator.list.type_descriptor.visit(iterator.list, live_objects);
    }
}

fn finalize(self: *Object, gc: *Gc) void {
    Object.Base.finalize(self, gc);
}

pub fn registerMethods(gc: *Gc, ic: *Class) !void {
    try ic.defineNativeMethod(gc, "__next__", next, 0, false);
}

fn next(vm: *Vm, self: *Object, _: []const Value) !Value {
    _ = vm;
    const iterator: *ListIterator = self.as(ListIterator);
    const list: *List = iterator.list.as(List);

    if (iterator.index >= list.items.items.len) {
        return Value.Undefined;
    }

    const item = list.items.items[iterator.index];
    iterator.index += 1;
    return item;
}
