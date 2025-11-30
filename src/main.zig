const std = @import("std");
const luna = @import("luna");

pub fn main() !void {
    std.debug.print("All your {s} are belong to us.\n", .{"codebase"});
    try luna.bufferedPrint();
}

test "simple test" {
    const gpa = std.testing.allocator;
    var list: std.ArrayList(i32) = .empty;
    defer list.deinit(gpa); // Try commenting this out and see if zig detects the memory leak!
    try list.append(gpa, 42);
    try std.testing.expectEqual(@as(i32, 42), list.pop());
}
