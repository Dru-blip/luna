const std = @import("std");
const Object = @import("Object.zig");
const Gc = @import("../core/Gc.zig");
const ConsoleObject = @import("ConsoleObject.zig");
const Value = @import("../core/Value.zig");

pub fn new(gc: *Gc) !*Object {
    var obj = try gc.alloc(Object.Base);
    var console_name = try gc.interpreter.string_interner.intern("console");
    const console = try ConsoleObject.new(gc);
    try obj.set(console_name.toPropertyKey(), Value.object(console));
    return obj;
}
