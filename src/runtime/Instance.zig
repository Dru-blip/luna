const std = @import("std");

const Class = @import("Class.zig");
const Gc = @import("../core/Gc.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const String = @import("String.zig");
const Value = @import("../core/Value.zig");

const Instance = @This();

attributes: Class.FieldMap,
class: *Class,

pub const type_descriptor: Object.TypeDescriptor = .{
    .name = "Instance",
    .visit = visit,
    .finalize = finalize,
};

pub fn getAttribute(self: *Instance, name: *String) ?Value {
    return self.attributes.fields.get(name);
}

pub fn setAttribute(self: *Instance, name: *String, value: Value) !void {
    try self.attributes.fields.put(name, value);
}

pub fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try live_objects.add(self);
    const instance: *Instance = self.as(Instance);
    const class = Object.from(instance.class);
    try class.type_descriptor.visit(class, live_objects);

    var iter = instance.attributes.iterator();
    while (iter.next()) |attribute| {
        const key = attribute.key_ptr.*;
        const key_obj = Object.from(key);
        if (!live_objects.contains(key_obj)) {
            try key_obj.type_descriptor.visit(key_obj, live_objects);
        }

        const value = attribute.value_ptr;
        if (value.asObject()) |obj| {
            if (!live_objects.contains(obj)) {
                try obj.type_descriptor.visit(obj, live_objects);
            }
        }
    }
}

pub fn finalize(self: *Object, _: *Gc) void {
    const instance: *Instance = self.as(Instance);
    instance.attributes.deinit();
}
