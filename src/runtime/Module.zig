const std = @import("std");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const String = @import("String.zig");
const ObjectSet = @import("ObjectSet.zig");
const Gc = @import("../core/Gc.zig");

const Module = @This();

exported: Value = Value.None,
name: *String,
raw_path: []u8,

pub fn new(gc: *Gc, name: []const u8) !*Module {
    const obj = try gc.alloc(Module);
    const module: *Module = obj.as(Module);
    obj.class = gc.interpreter.base_class;
    module.* = .{
        .raw_path = try gc.gpa.dupe(u8, name),
        .exported = Value.None,
        .name = (try String.new(gc, name)).as(String),
    };
    return module;
}

pub const type_descriptor: Object.TypeDescriptor = .{
    .name = "Module",
    .visit = visit,
    .finalize = finalize,
};

pub fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const module: *Module = self.as(Module);
    if (module.exported.asObject()) |obj| {
        try obj.type_descriptor.visit(obj, live_objects);
    }
    const name = Object.from(module.name);
    try name.type_descriptor.visit(name, live_objects);
}

pub fn finalize(self: *Object, gc: *Gc) void {
    const module: *Module = self.as(Module);
    gc.gpa.free(module.raw_path);
}
