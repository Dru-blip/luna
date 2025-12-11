const std = @import("std");
const Gc = @import("../Gc.zig");
const Value = @import("../Value.zig");
const Ast = @import("../Ast.zig");

const StringInterner = @import("StringInterner.zig");
const Vm = @import("Vm.zig");
const Generator = @import("../Generator.zig");

const Interpreter = @This();

gc: Gc,
string_interner: StringInterner,
vm: *Vm = undefined,

pub fn init(gpa: std.mem.Allocator) !*Interpreter {
    var gc = Gc.init(gpa);
    var interpreter = try gpa.create(Interpreter);

    interpreter.* = .{
        .gc = gc,
        .string_interner = StringInterner.init(&gc),
    };

    interpreter.vm = try Vm.init(gpa, interpreter);

    return interpreter;
}

pub fn runFile(i: *Interpreter, path: []const u8) !Value {
    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();
    const file_stats = try file.stat();
    var buffer = try i.gc.gpa.alloc(u8, file_stats.size + 1);

    _ = try file.readAll(buffer);
    buffer[file_stats.size] = 0;

    var ast = try Ast.parse(buffer[0..file_stats.size :0], i.gc.gpa);
    defer ast.deinit();

    var generator = try Generator.init(i.gc.gpa, ast, &i.gc);
    const executable = try generator.generate();

    const result = try i.vm.runExecutable(executable);
    std.debug.print("result: {d}\n", .{result.data.int});

    return Value.none();
}
