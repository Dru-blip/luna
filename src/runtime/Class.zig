const std = @import("std");
const Gc = @import("../core/Gc.zig");
const String = @import("String.zig");
const Value = @import("../core/Value.zig");
const NativeFunction = @import("NativeFunction.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Instance = @import("Instance.zig");

const Class = @This();

super_class: ?*Class = null,
name: *String,
methods: FieldMap,

pub inline fn new(gc: *Gc) !*Class {
    const obj = try gc.alloc(Class);
    obj.class = undefined;
    var class: *Class = obj.as(Class);
    class.super_class = null;
    class.methods = FieldMap.init(gc);
    return class;
}

pub inline fn newInstance(class: *Class, gc: *Gc) !*Object {
    const obj = try gc.alloc(Instance);
    obj.class = class;
    const instance: *Instance = obj.as(Instance);
    instance.* = .{
        .attributes = FieldMap.init(gc),
        .class = class,
    };
    return obj;
}

pub fn defineNativeMethod(class: *Class, gc: *Gc, name: []const u8, func: NativeFunction.Function, arity: u8, isVariadic: bool) !void {
    const interned_name = try gc.interpreter.string_interner.intern(name);
    const nf = try NativeFunction.new(gc, class, func, interned_name, arity, isVariadic);
    try class.putField(interned_name, Value.object(nf));
}

pub fn putField(class: *Class, name: *String, value: Value) !void {
    try class.methods.fields.put(name, value);
}

pub fn getField(class: *Class, name: *String) ?Value {
    var current_class: ?*Class = class;
    while (current_class) |c| {
        if (c.methods.fields.get(name)) |value| {
            return value;
        }
        current_class = c.super_class;
    }
    return null;
}

pub fn addMethod(class: *Class, name: *String, function: Value) !void {
    try class.methods.fields.put(name, function);
}

pub const type_descriptor: Object.TypeDescriptor = .{
    .name = "Class",
    .visit = visit,
    .finalize = finalize,
};

pub fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try live_objects.add(self);
    const class: *Class = self.as(Class);

    if (class.super_class) |sc| {
        const super_class_obj = Object.from(sc);
        if (!live_objects.contains(super_class_obj)) {
            try super_class_obj.type_descriptor.visit(super_class_obj, live_objects);
        }
    }

    var iterator = class.methods.iterator();

    while (iterator.next()) |entry| {
        const key = entry.key_ptr.*;
        const key_obj = Object.from(key);
        if (!live_objects.contains(key_obj)) {
            try key_obj.type_descriptor.visit(key_obj, live_objects);
        }

        const value = entry.value_ptr;
        if (value.asObject()) |obj| {
            if (!live_objects.contains(obj)) {
                try obj.type_descriptor.visit(obj, live_objects);
            }
        }
    }
}

pub fn finalize(self: *Object, _: *Gc) void {
    const class: *Class = self.as(Class);
    class.methods.deinit();
}

pub const FieldMap = struct {
    const Map = std.HashMap(*String, Value, FieldContext, std.hash_map.default_max_load_percentage);

    fields: Map,

    const FieldContext = struct {
        pub fn hash(_: @This(), s: *String) u64 {
            return s.hash;
        }
        pub fn eql(_: @This(), a: *String, b: *String) bool {
            return a.eql(b);
        }
    };

    pub fn init(gc: *Gc) FieldMap {
        const map = Map.init(gc.gpa);
        return FieldMap{ .fields = map };
    }

    pub fn deinit(self: *FieldMap) void {
        self.fields.deinit();
    }

    pub inline fn iterator(self: *FieldMap) Map.Iterator {
        return self.fields.iterator();
    }
};
