const std = @import("std");
const Gc = @import("../core/Gc.zig");

const String = @import("String.zig");
const Object = @import("Object.zig");
const Class = @import("Class.zig");

const BaseExceptionClass = @This();

pub fn MakeExceptionClass(gc: *Gc, name: *String, super: ?*Class) !*Class {
    const class: *Class = try Class.new(gc);
    class.name = name;
    class.super_class = super;
    return class;
}
