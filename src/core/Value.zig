const std = @import("std");

type: Type,
data: Data,

pub const Type = enum {
    int,
};

pub const Data = union(Type) {
    int: i64,
};
