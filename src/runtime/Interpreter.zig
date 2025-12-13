const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Ast = @import("../core/Ast.zig");

const StringInterner = @import("StringInterner.zig");
const Vm = @import("Vm.zig");
const Generator = @import("../core/Generator.zig");

const luna = @import("luna");

const Interpreter = @This();

gc: Gc,
string_interner: StringInterner = undefined,
vm: *Vm = undefined,
// running_module: *Module,

pub fn init(gpa: std.mem.Allocator) !*Interpreter {
    var interpreter = try gpa.create(Interpreter);
    const gc = Gc.init(gpa, interpreter);

    interpreter.* = .{
        .gc = gc,
    };

    interpreter.vm = try Vm.init(gpa, interpreter);
    interpreter.string_interner = StringInterner.init(&interpreter.gc);

    return interpreter;
}

pub fn runFile(i: *Interpreter, path: []const u8) !Value {
    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();
    const file_stats = try file.stat();
    var buffer = try i.gc.gpa.alloc(u8, file_stats.size + 1);

    //TODO: should switch to new reader implementation.
    _ = try file.readAll(buffer);
    buffer[file_stats.size] = 0;

    var ast = Ast.parse(buffer[0..file_stats.size :0], i.gc.gpa) catch {
        return Value.none();
    };
    defer ast.deinit();

    var generator = try Generator.init(i.gc.gpa, ast, &i.gc, &i.string_interner);
    const executable = try generator.generate();
    try executable.print();

    const result = try i.vm.runExecutable(executable);
    std.debug.print("result: {d}\n", .{result.data.int});

    try i.gc.collectGarbage();

    return Value.none();
}
