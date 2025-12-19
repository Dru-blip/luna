const std = @import("std");
const Object = @import("Object.zig");
const Gc = @import("../core/Gc.zig");
const ConsoleObject = @import("ConsoleObject.zig");
const Value = @import("../core/Value.zig");

pub fn new(gc: *Gc) !*Object {
    const global = try gc.alloc(Object.Base);

    const console_key = try gc.interpreter.string_interner.intern("console");

    const console_obj = try ConsoleObject.new(gc);

    try global.set(console_key.toPropertyKey(), Value.object(console_obj));

    return global;
}
