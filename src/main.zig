const std = @import("std");
const Interpreter = @import("runtime/Interpreter.zig");

// TODO: should flatten the inheritance chain.
// TODO: exception chain.
// TODO: type speculation , switching opcodes.
pub fn main() !void {
    var gpa = std.heap.DebugAllocator(.{}){};
    defer {
        // _ = gpa.deinit();
    }

    const allocator = gpa.allocator();
    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    var interpreter = try Interpreter.init(allocator);
    _ = try interpreter.runFile(args[1]);

    //TODO: free memory
    interpreter.deinit();
}
