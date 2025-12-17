const std = @import("std");

const String = @import("String.zig");
const Value = @import("../core/Value.zig");

pub const PropertyKey = union(enum) {
    int: i64,
    string: *String,

    pub fn fromInt(value: i64) PropertyKey {
        return .{ .int = value };
    }

    pub fn fromString(value: *String) PropertyKey {
        return .{ .string = value };
    }

    pub fn isString(self: *PropertyKey) bool {
        return switch (self.*) {
            .string => true,
            .int => false,
        };
    }
};

pub const PropertyMap = struct {
    const Storage = std.HashMap(PropertyKey, Value, PropertyContext, std.hash_map.default_max_load_percentage);
    pub const Iterator = Storage.Iterator;
    pub const KeyIterator = Storage.KeyIterator;
    pub const ValueIterator = Storage.ValueIterator;

    gpa: std.mem.Allocator,
    storage: Storage,

    pub fn init(gpa: std.mem.Allocator) PropertyMap {
        return .{
            .gpa = gpa,
            .storage = Storage.init(gpa),
        };
    }

    pub fn deinit(self: *PropertyMap) void {
        self.storage.deinit();
    }

    pub inline fn put(self: *PropertyMap, key: PropertyKey, value: Value) !void {
        try self.storage.put(key, value);
    }

    pub inline fn get(self: *PropertyMap, key: PropertyKey) ?Value {
        return self.storage.get(key);
    }

    pub fn iterator(self: *PropertyMap) Iterator {
        return self.storage.iterator();
    }

    pub fn keyIterator(self: *PropertyMap) KeyIterator {
        return self.storage.keyIterator();
    }

    pub fn valueIterator(self: *PropertyMap) ValueIterator {
        return self.storage.valueIterator();
    }

    const PropertyContext = struct {
        pub fn hash(_: @This(), prop: PropertyKey) u64 {
            return switch (prop) {
                .int => |i| @intCast(i),
                .string => |s| s.hash,
            };
        }
        pub fn eql(_: @This(), a: PropertyKey, b: PropertyKey) bool {
            return switch (a) {
                .int => |i| {
                    return switch (b) {
                        .int => |j| i == j,
                        .string => |_| false,
                    };
                },
                .string => |s| {
                    return switch (b) {
                        .int => |_| false,
                        .string => |t| s.eql(t),
                    };
                },
            };
        }
    };
};
