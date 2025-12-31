const std = @import("std");

const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Class = @import("Class.zig");
const String = @import("String.zig");

const Gc = @import("../core/Gc.zig");
const Executable = @import("../core/bytecode.zig").Executable;
const ModuleEnvironment = @import("environments/ModuleEnvironment.zig");

const Function = @This();

arity: u8,
is_variadic: bool = false,
module: *Object = undefined,
exe: *Executable,
home_class: ?*Class = null,
module_env: *ModuleEnvironment = undefined,

pub fn withExecutable(gc: *Gc, executable: *Executable) !*Object {
    const obj = try gc.alloc(Function);
    obj.class = gc.interpreter.base_class;
    const function = obj.as(Function);
    function.* = .{
        .arity = executable.param_count,
        .exe = executable,
    };
    return obj;
}

pub const type_descriptor: Object.TypeDescriptor = .{
    .name = "Function",
    .visit = visit,
    .finalize = finalize,
};

fn finalize(self: *Object, gc: *Gc) void {
    Object.Base.finalize(self, gc);
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const function: *Function = self.as(Function);
    const exe_obj = Object.from(function.exe);
    if (!live_objects.contains(exe_obj)) {
        try exe_obj.type_descriptor.visit(exe_obj, live_objects);
    }
    if (function.home_class) |home_class| {
        try Object.from(home_class).type_descriptor.visit(Object.from(home_class), live_objects);
    }
}
