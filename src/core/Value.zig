const std = @import("std");
const GcObject = @import("Gc.zig").GcObject;

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
    object: *GcObject,
};

pub fn fromInt(value: i64) Value {
    return .{ .type = .int, .data = .{ .int = value } };
}
