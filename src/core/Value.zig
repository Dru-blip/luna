const std = @import("std");
const GcObject = @import("Gc.zig").GcObject;

type: Type,
data: Data,

pub const Type = enum {
    int,
    object,
};

pub const Data = union(Type) {
    int: i64,
    object: *GcObject,
};
