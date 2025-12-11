const std = @import("std");
const Object = @import("runtime/Object.zig");
const PropertyKey = @import("runtime/property_map.zig").PropertyKey;

const Value = @This();

type: Type,
data: Data,

pub const Type = enum {
    int,
    bool,
    none,
    undefined,
    object,
};

pub const Data = union(Type) {
    int: i64,
    bool: bool,
    none: void,
    undefined: void,
    object: *Object,
};

pub inline fn int(value: i64) Value {
    return .{
        .type = .int,
        .data = .{
            .int = value,
        },
    };
}

pub inline fn @"bool"(value: bool) Value {
    return .{
        .type = .bool,
        .data = .{
            .bool = value,
        },
    };
}

pub inline fn none() Value {
    return .{
        .type = .none,
        .data = .{
            .none = {},
        },
    };
}

pub inline fn @"undefined"() Value {
    return .{
        .type = .undefined,
        .data = .{
            .undefined = {},
        },
    };
}

pub inline fn isInt(value: Value) bool {
    return value.type == .int;
}

pub inline fn isBool(value: Value) bool {
    return value.type == .bool;
}

pub inline fn isObject(value: Value) bool {
    return value.type == .object;
}

pub inline fn isNone(value: Value) bool {
    return value.type == .none;
}

pub inline fn isUndefined(value: Value) bool {
    return value.type == .undefined;
}

pub inline fn toInt(value: Value) i64 {
    return value.data.int;
}

pub inline fn toBool(value: Value) bool {
    return value.data.bool;
}

pub inline fn toObject(value: Value) *Object {
    return value.data.object;
}

pub fn toPropertyKey(value: Value) ?PropertyKey {
    //TODO: raise error if value is not an integer or string
    switch (value.type) {
        .int => PropertyKey.fromInt(value.data.int),
        else => null,
    }
}
