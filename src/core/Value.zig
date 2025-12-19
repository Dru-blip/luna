const std = @import("std");
const Object = @import("../runtime/Object.zig");
const PropertyKey = @import("../runtime/property_map.zig").PropertyKey;
const String = @import("../runtime/String.zig");

const Value = @This();

type: Type,
data: Data,

pub const Type = enum {
    int,
    float,
    bool,
    none,
    undefined,
    object,
};

pub const Data = union(Type) {
    int: i64,
    float: f64,
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

pub inline fn float(value: f64) Value {
    return .{
        .type = .float,
        .data = .{
            .float = value,
        },
    };
}

pub inline fn @"bool"(value: bool) Value {
    return if (value) True else False;
}

pub const None: Value = .{
    .type = .none,
    .data = .{
        .none = {},
    },
};

pub const Undefined: Value = .{
    .type = .undefined,
    .data = .{
        .undefined = {},
    },
};

pub const True: Value = .{
    .type = .bool,
    .data = .{
        .bool = true,
    },
};

pub const False: Value = .{
    .type = .bool,
    .data = .{
        .bool = false,
    },
};

pub inline fn object(obj: *Object) Value {
    return .{
        .type = .object,
        .data = .{
            .object = obj,
        },
    };
}

pub inline fn isInt(value: Value) bool {
    return value.type == .int;
}

pub inline fn isFloat(value: Value) bool {
    return value.type == .float;
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

pub inline fn asObject(value: Value) ?*Object {
    return if (value.type == .object) value.data.object else null;
}

pub inline fn asInt(value: Value) i64 {
    return switch (value.type) {
        .int => value.data.int,
        .bool => @intFromBool(value.data.bool),
        else => unreachable,
    };
}

pub inline fn asFloat(value: Value) f64 {
    return switch (value.type) {
        .float => value.data.float,
        .int => @as(f64, @floatFromInt(value.data.int)),
        .bool => @as(f64, @floatFromInt(@intFromBool(value.data.bool))),
        else => unreachable,
    };
}

pub inline fn isFalsy(v: Value) bool {
    return v.type == .none or (v.type == .bool and v.data.bool == false);
}

pub inline fn isTruthy(v: Value) bool {
    return !isFalsy(v);
}

pub inline fn isInteger(value: Value) bool {
    return value.type == .int or value.type == .bool;
}

pub inline fn isNumeric(value: Value) bool {
    return value.type == .int or value.type == .float or value.type == .bool;
}

pub inline fn getTypeString(v: Value) []const u8 {
    return switch (v.type) {
        .int => "int",
        .bool => "bool",
        .float => "float",
        .object => "object",
        .none => "none",
        .undefined => "undefined",
    };
}

pub inline fn toPropertyKey(value: Value) ?PropertyKey {
    return switch (value.type) {
        .int => PropertyKey.fromInt(value.data.int),
        .object => {
            const obj = value.toObject();
            if (obj.asString()) |string| {
                return PropertyKey.fromString(string);
            }
            return null;
        },
        else => null,
    };
}

pub fn eql(a: Value, b: Value) bool {
    if (a.type != b.type) return false;
    return switch (a.type) {
        .int => a.data.int == b.data.int,
        .float => a.data.float == b.data.float,
        .bool => a.data.bool == b.data.bool,
        .none, .undefined => true,
        .object => a.data.object == b.data.object,
    };
}
