const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Object = @import("Object.zig");

const StringClass = @This();

pub fn new(gc: *Gc) !*Class {
    const sc = try Class.new(gc);
    var obj = Object.from(sc);
    obj.class = gc.interpreter.base_class;
    sc.super_class = gc.interpreter.base_class;
    return sc;
}

pub fn registerMethods(gc: *Gc, sc: *Class) !void {
    try sc.defineNativeMethod(gc, "at", at, 1, false);
}

fn at(vm: *Vm, self: *Object, args: []const Value) !Value {
    _ = vm;
    _ = self;
    _ = args;
    // TODO: implement
    return Value.None;
}
