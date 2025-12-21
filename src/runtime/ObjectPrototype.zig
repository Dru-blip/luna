const std = @import("std");

const Object = @import("Object.zig");
const Gc = @import("../core/Gc.zig");

const Vm = @import("Vm.zig");
const Value = @import("../core/Value.zig");

const ObjectPrototype = @This();

pub fn new(gc: *Gc) !*Object {
    var obj = try gc.alloc(Object.Base);
    try obj.defineNativeFunction(gc, "toString", toString, 0, false);
    try obj.defineProperty(gc, "prototype", Value.None);
    return obj;
}

fn toString(vm: *Vm, _: *Object, _: []Value) !Value {
    const string = try vm.interpreter.string_interner.intern("Object");
    return Value.object(Object.from(string));
}
