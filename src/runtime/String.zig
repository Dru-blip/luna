const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Interpreter = @import("Interpreter.zig");
// const PropertyKey = @import("property_map.zig").PropertyKey;

const StringPrototype = @import("StringPrototype.zig");

const String = @This();

const max_small_string_len = 32;

hash: u64,
length: usize,
storage: Storage,
interned: bool = false,

const Storage = union(enum) {
    @"inline": [max_small_string_len]u8,
    heap: []const u8,
};

pub const type_descriptor = Object.TypeDescriptor{
    .name = "String",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc, bytes: []const u8) !*Object {
    var obj = try gc.alloc(String);
    obj.prototype = gc.interpreter.string_prototype;
    var s: *String = obj.as(String);

    s.length = bytes.len;
    s.hash = std.hash.Wyhash.hash(0, bytes);

    if (bytes.len <= max_small_string_len) {
        s.storage = .{ .@"inline" = undefined };
        @memcpy(s.storage.@"inline"[0..bytes.len], bytes);
    } else {
        const buf = try gc.gpa.alloc(u8, bytes.len);
        @memcpy(buf, bytes);
        s.storage = .{ .heap = buf };
    }
    return obj;
}

pub inline fn asSlice(self: *const String) []const u8 {
    return switch (self.storage) {
        .@"inline" => |buf| buf[0..self.length],
        .heap => |buf| buf,
    };
}

pub fn eql(a: *const String, b: *const String) bool {
    if (a == b) return true;
    if (a.length != b.length) return false;
    return std.mem.eql(u8, a.asSlice(), b.asSlice());
}

fn finalize(self: *Object, gc: *Gc) void {
    const str: *String = self.as(String);

    switch (str.storage) {
        .heap => |slice| gc.gpa.free(slice),
        else => {},
    }
    Object.Base.finalize(self, gc);
}

fn visit(self: *Object, live_objects: *ObjectSet) std.mem.Allocator.Error!void {
    try Object.Base.visit(self, live_objects);
}
