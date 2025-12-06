const std = @import("std");
const Tokenizer = @import("core/Tokenizer.zig");
const Parser = @import("core/Parser.zig");
const Eval = @import("core/Eval.zig");
const Gc = @import("core/Gc.zig");
const LuObject = @import("core/LuObject.zig");
const Generator = @import("core/Generator.zig");

pub fn main() !void {
    var gpa = std.heap.DebugAllocator(.{}){};
    defer {
        const deinit_status = gpa.deinit();
        if (deinit_status == .leak) {
            std.debug.print("Memory leak detected\n", .{});
        }
    }
    const source = "10/5";
    const allocator = gpa.allocator();
    const tokens = try Tokenizer.tokenize(source, allocator);
    var parser = Parser.init(allocator, source, tokens);
    var ast = try parser.parse();
    defer ast.deinit();
    var gc = Gc.init(allocator);
    defer gc.deinit();
    var generator = try Generator.init(allocator, ast, &gc);
    defer generator.deinit();
    _ = try generator.generate();

    // Eval.eval(ast);
}
