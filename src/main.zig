const std = @import("std");
const Tokenizer = @import("core/Tokenizer.zig");
const Parser = @import("core/Parser.zig");

pub fn main() !void {
    var gpa = std.heap.DebugAllocator(.{}){};
    defer {
        const deinit_status = gpa.deinit();
        if (deinit_status == .leak) {
            std.debug.print("Memory leak detected\n", .{});
        }
    }
    const allocator = gpa.allocator();
    var tokens = try Tokenizer.tokenize("hello 5", allocator);
    defer tokens.deinit(allocator);
    _ = Parser.init(allocator, "hello 5", tokens);
}
