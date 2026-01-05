const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const String = @import("String.zig");

const StringIterator = @This();

string: *Object,
index: usize,

pub const gc_hooks = Object.GcHooks{
    .name = "StringIterator",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc) !*Class {
    const ic = try Class.new(gc);
    ic.super_class = gc.interpreter.base_class;
    return ic;
}

pub fn newInstance(gc: *Gc, string_obj: *Object) !*Object {
    const obj = try gc.alloc(StringIterator);
    obj.class = gc.interpreter.string_iterator_class;
    var iterator: *StringIterator = obj.as(StringIterator);
    iterator.string = string_obj;
    iterator.index = 0;
    return obj;
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const iterator: *StringIterator = self.as(StringIterator);

    if (!live_objects.contains(iterator.string)) {
        try iterator.string.gc_hooks.visit(iterator.string, live_objects);
    }
}

fn finalize(_: *Object, _: *Gc) void {}

pub fn registerMethods(gc: *Gc, ic: *Class) !void {
    try ic.defineNativeMethod(gc, "__next__", next, 0, false);
}

fn next(vm: *Vm, self: *Object, _: []const Value) !Value {
    const iterator: *StringIterator = self.as(StringIterator);
    const str: *String = iterator.string.as(String);
    const slice = str.asSlice();

    if (iterator.index >= slice.len) {
        return Value.Undefined;
    }

    const byte = slice[iterator.index];
    iterator.index += 1;
    return Value.object(try String.new(vm.gc, &[1]u8{byte}));
}
