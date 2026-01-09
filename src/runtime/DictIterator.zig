const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Dict = @import("Dict.zig");
const String = @import("String.zig");

const DictIterator = @This();

dict: *Object,
iterator: Dict.Iterator(.Entry),

pub const gc_hooks = Object.GcHooks{
    .name = "DictIterator",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc) !*Class {
    const ic = try Class.new(gc);
    //TODO: Should switch to base iterator class
    ic.super_class = gc.interpreter.base_class;
    return ic;
}

pub fn newInstance(gc: *Gc, dict_obj: *Object) !*Object {
    const obj = try gc.alloc(DictIterator);
    obj.class = gc.interpreter.dict_iterator_class;
    var iterator: *DictIterator = obj.as(DictIterator);
    iterator.dict = dict_obj;
    iterator.iterator = dict_obj.as(Dict).map.iterator();
    return obj;
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const iterator: *DictIterator = self.as(DictIterator);

    if (!live_objects.contains(iterator.dict)) {
        try iterator.dict.gc_hooks.visit(iterator.dict, live_objects);
    }
}

fn finalize(_: *Object, _: *Gc) void {
    // Object.Base.finalize(self, gc);
}

pub fn registerMethods(gc: *Gc, ic: *Class) !void {
    try ic.defineNativeMethod(gc, "__next__", next, 0, false);
}

fn next(_: *Vm, self: *Object, _: []const Value) !Value {
    const iterator: *DictIterator = self.as(DictIterator);
    const entry = iterator.iterator.next() orelse return Value.Undefined;
    return entry.key;
}
