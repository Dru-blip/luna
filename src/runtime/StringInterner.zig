const std = @import("std");

const Gc = @import("../core/Gc.zig");

const String = @import("String.zig");

const StringInterner = @This();

strings: std.StringHashMap(*String),
gc: *Gc,

pub fn init(gc: *Gc) StringInterner {
    return .{
        .strings = std.StringHashMap(*String).init(gc.gpa),
        .gc = gc,
    };
}

pub fn deinit(interner: *StringInterner) void {
    interner.strings.deinit();
}

pub fn intern(interner: *StringInterner, bytes: []const u8) !*String {
    if (interner.strings.get(bytes)) |existing| {
        return existing;
    }

    var string = try String.new(interner.gc, bytes);
    string.interned = true;
    try interner.strings.put(bytes, string);

    return string;
}
