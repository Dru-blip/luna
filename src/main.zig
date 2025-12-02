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
    var tokens = try Tokenizer.tokenize("return 5+6", allocator);
    defer tokens.deinit(allocator);
    var parser = Parser.init(allocator, "return 5+6", tokens);
    var ast = try parser.parse();
    defer ast.deinit();
    std.debug.print("nodes: {d}\n", .{ast.nodes.items.len});
}
