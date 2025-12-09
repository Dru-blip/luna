const std = @import("std");

const String = @import("String.zig");
const Value = @import("../Value.zig");

pub const PropertyKey = union(enum) {
    int: i64,
    string: *String,

    pub fn fromInt(value: i64) PropertyKey {
        return .{ .int = value };
    }
};

pub const PropertyMap = struct {
    const Storage = std.HashMap(PropertyKey, Value, PropertyContext, std.hash_map.default_max_load_percentage);

    gpa: std.mem.Allocator,
    storage: Storage,

    pub fn init(gpa: std.mem.Allocator) PropertyMap {
        return PropertyMap{
            .gpa = gpa,
            .storage = Storage.init(gpa),
        };
    }

    const PropertyContext = struct {
        pub fn hash(_: @This(), prop: PropertyKey) u64 {
            return switch (prop.*) {
                .int => |i| i,
                .string => |s| s.hash,
            };
        }
        pub fn eql(_: @This(), a: PropertyKey, b: PropertyKey) bool {
            return switch (a.*) {
                .int => |i| {
                    switch (b.*) {
                        .int => |j| i == j,
                        .string => |_| false,
                    }
                },
                .string => |s| {
                    switch (b.*) {
                        .int => |_| false,
                        .string => |t| s.eql(t),
                    }
                },
            };
        }
    };
};
