const std = @import("std");
const Gc = @import("../core/Gc.zig");

const String = @import("String.zig");
const Object = @import("Object.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Exception = @import("Exception.zig");
const Value = @import("../core/Value.zig");

const BaseExceptionClass = @This();

pub fn MakeExceptionClass(gc: *Gc, name: *String, super: ?*Class) !*Class {
    const class: *Class = try Class.new(gc);
    class.name = name;
    class.super_class = super;
    class.constructor = constructor;
    return class;
}

fn constructor(vm: *Vm, self: *Object, args: []const Value) !Value {
    const msg = args[0].toObject().toString();
    const exception = try Exception.new(vm.gc, self.as(Class), msg);
    try exception.buildTraceback(vm);
    return Value.object(Object.from(exception));
}
