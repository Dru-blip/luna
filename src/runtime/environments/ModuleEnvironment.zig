const Gc = @import("../../core/Gc.zig");
const Value = @import("../../core/Value.zig");
const Object = @import("../Object.zig");
const ObjectSet = @import("../ObjectSet.zig");
const Module = @import("../Module.zig");
const Vm = @import("../Vm.zig");

const ModuleEnvironment = @This();

module: *Module,
globals: Vm.Globals,

pub const type_descriptor: Object.TypeDescriptor = .{
    .name = "ModuleEnvironment",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc, module: *Module, global_count: u32) !*ModuleEnvironment {
    const obj = try gc.alloc(ModuleEnvironment);
    var env: *ModuleEnvironment = obj.as(ModuleEnvironment);
    env.module = module;
    env.globals = .{
        .fast_slots = try gc.gpa.alloc(Value, global_count),
        .named_slots = undefined,
    };
    return env;
}

pub fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const env: *ModuleEnvironment = self.as(ModuleEnvironment);
    const module_obj = Object.from(env.module);
    try module_obj.type_descriptor.visit(module_obj, live_objects);

    for (env.globals.fast_slots) |global| {
        if (global.asObject()) |obj| {
            try obj.type_descriptor.visit(obj, live_objects);
        }
    }
}

pub fn finalize(self: *Object, gc: *Gc) void {
    const env: *ModuleEnvironment = self.as(ModuleEnvironment);
    gc.gpa.free(env.globals.fast_slots);
}
