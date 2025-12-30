const std = @import("std");
const Interpreter = @import("runtime/Interpreter.zig");

//TODO: should flatten the inheritance chain.
pub fn main() !void {
    var gpa = std.heap.DebugAllocator(.{}){};
    defer {
        _ = gpa.deinit();
        // if (deinit_status == .leak) {
        //     std.debug.print("Memory leak detected\n", .{});
        // }
    }

    const allocator = gpa.allocator();
    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    var interpreter = try Interpreter.init(allocator);
    _ = try interpreter.runFile(args[1]);

    //TODO: free memory
    interpreter.deinit();
}
