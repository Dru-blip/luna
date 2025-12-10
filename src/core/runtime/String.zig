const std = @import("std");
const Gc = @import("../Gc.zig");
const Object = @import("Object.zig");

const String = @This();

const max_small_string_len = 32;

hash: u64,
length: usize,
storage: Storage,
interned: bool,

const Storage = union(enum) {
    @"inline": [max_small_string_len]u8,
    heap: []const u8,
};

const type_descriptor = Object.TypeDescriptor{
    .name = "String",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc, bytes: []const u8) !*String {
    var s = try gc.alloc(String);

    s.length = bytes.len;
    s.hash = std.hash.Wyhash.hash(0, bytes);
    s.interned = false;

    if (bytes.len <= max_small_string_len) {
        @memcpy(s.storage.@"inline"[0..bytes.len], bytes);
    } else {
        const buf = try gc.gpa.alloc(u8, bytes.len);
        @memcpy(buf, bytes);
        s.storage = .{ .heap = buf };
    }
    return s;
}

pub inline fn asSlice(self: *const String) []const u8 {
    return switch (self.storage.*) {
        .@"inline" => |buf| buf[0..self.length],
        .heap => |buf| buf,
    };
}

pub fn eql(a: *const String, b: *const String) bool {
    if (a == b) return true;
    if (a.length != b.length) return false;
    return std.mem.eql(u8, a.asSlice(), b.asSlice());
}

fn finalize(self: *anyopaque, gc: *Gc) void {
    const str: *String = @ptrCast(@alignCast(self));

    switch (str.storage.*) {
        .heap => |slice| gc.gpa.free(slice),
        else => {},
    }
}

fn visit(self: *anyopaque, gc: *Gc) void {
    _ = self;
    _ = gc;
}
