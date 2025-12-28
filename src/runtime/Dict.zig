const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Class = @import("Class.zig");
const String = @import("String.zig");
const Vm = @import("Vm.zig");

const Dict = @This();

const Map = std.HashMap(Value, Value, ValueContext, std.hash_map.default_max_load_percentage);

const ValueContext = struct {
    pub fn hash(_: @This(), key: Value) u64 {
        switch (key.type) {
            .number => return @as(u64, @bitCast(key.data.number)),
            .bool => return @as(u64, @intFromBool(key.data.bool)),
            .none, .undefined => return 0,
            .object => {
                if (key.toObject().asString()) |str| {
                    return str.hash;
                }
                //TODO: should call __hash__ function of object.
                return @intFromPtr(key.toObject());
            },
        }
    }

    pub fn eql(_: @This(), a: Value, b: Value) bool {
        return a.eql(b);
    }
};

map: Map,

pub const type_descriptor = Object.TypeDescriptor{
    .name = "Dict",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc) !*Object {
    const obj = try gc.alloc(Dict);
    obj.class = gc.interpreter.dict_class;
    var dict: *Dict = obj.as(Dict);

    dict.map = Map.init(gc.gpa);
    return obj;
}

pub fn set(dict: *Dict, key: Value, value: Value) !void {
    try dict.map.put(key, value);
}

pub fn get(dict: *Dict, key: Value) ?Value {
    return dict.map.get(key);
}

pub fn remove(dict: *Dict, key: Value) bool {
    return dict.map.remove(key);
}

pub fn contains(dict: *Dict, key: Value) bool {
    return dict.map.contains(key);
}

pub fn clear(dict: *Dict) void {
    dict.map.clearAndFree();
}

pub fn size(dict: *Dict) usize {
    return dict.map.count();
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const dict: *Dict = self.as(Dict);

    var iterator = dict.map.iterator();
    while (iterator.next()) |entry| {
        if (entry.key_ptr.asObject()) |key_obj| {
            if (!live_objects.contains(key_obj)) {
                try key_obj.type_descriptor.visit(key_obj, live_objects);
            }
        }
        if (entry.value_ptr.asObject()) |value_obj| {
            if (!live_objects.contains(value_obj)) {
                try value_obj.type_descriptor.visit(value_obj, live_objects);
            }
        }
    }
}

fn finalize(self: *Object, gc: *Gc) void {
    const dict: *Dict = self.as(Dict);
    dict.map.deinit();
    Object.Base.finalize(self, gc);
}
