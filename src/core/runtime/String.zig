const std = @import("std");
const Gc = @import("../Gc.zig");

const String = @This();

hash: u64,
length: usize,
data: Data = .none,

pub const Data = union(enum) {
    none: void,
    interned: []u8,
    small: [32]u8,
    simple: []u8,
};

const max_small_string_len = 32;

const vtable = Gc.GcObject.VTable{
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc, bytes: []u8) !*String {
    var string: *String = try gc.allocWithVtable(String, &vtable);
    string.hash = std.hash.Wyhash.hash(0, bytes);
    string.length = bytes.len;

    if (string.length <= max_small_string_len) {
        @memcpy(string.data.small, bytes);
    } else {
        string.data.simple = try gc.gpa.alloc(u8, string.length);
        @memcpy(string.data.simple, bytes);
    }
    return string;
}

fn asSlice(s: *String) []const u8 {
    return switch (s.data.*) {
        .interned => |v| v,
        .small => |v| v,
        .simple => |v| v,
    };
}

pub fn eql(self: *String, other: *String) bool {
    if (self == other) return true;
    if (self.length != other.length) return false;

    const a = self.asSlice();
    const b = other.asSlice();

    return std.mem.eql(u8, a, b);
}

fn finalize(self: *anyopaque, gc: *Gc) void {
    const str: *String = @ptrCast(@alignCast(self));
    switch (str.data.*) {
        .simple => |slice| gc.gpa.free(slice),
        else => {},
    }
}

fn visit(self: *anyopaque, gc: *Gc) void {
    _ = self;
    _ = gc;
}
