const std = @import("std");
const Object = @import("runtime/Object.zig");
const PropertyKey = @import("runtime/property_map.zig").PropertyKey;

const Value = @This();

type: Type,
data: Data,

pub const Type = enum {
    int,
    bool,
    object,
};

pub const Data = union(Type) {
    int: i64,
    bool: bool,
    object: *Object,
};

pub fn fromInt(value: i64) Value {
    return .{ .type = .int, .data = .{ .int = value } };
}

pub fn toPropertyKey(value: Value) ?PropertyKey {
    //TODO: raise error if value is not an integer or string
    switch (value.type) {
        .int => PropertyKey.fromInt(value.data.int),
        else => null,
    }
}
