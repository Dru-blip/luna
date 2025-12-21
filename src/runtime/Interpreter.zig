const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Ast = @import("../core/Ast.zig");
const StringInterner = @import("StringInterner.zig");
const Vm = @import("Vm.zig");
const Generator = @import("../core/Generator.zig");
const Exception = @import("Exception.zig");
const Object = @import("Object.zig");
const ObjectPrototype = @import("ObjectPrototype.zig");
const StringPrototype = @import("StringPrototype.zig");

const GlobalObject = @import("GlobalObject.zig");

const Interpreter = @This();

pub const Error = error{ ExceptionThrown, RegisterPoolExhausted } || std.mem.Allocator.Error;

gc: Gc,
string_interner: StringInterner = undefined,
vm: *Vm = undefined,
exception: ?*Exception = null,
builtins: *Object = undefined,
object_prototype: *Object = undefined,
string_prototype: *Object = undefined,

// running_module: *Module,

pub fn init(gpa: std.mem.Allocator) !*Interpreter {
    var interpreter = try gpa.create(Interpreter);
    const gc = Gc.init(gpa, interpreter);

    interpreter.* = .{
        .gc = gc,
    };

    interpreter.vm = try Vm.init(gpa, interpreter);
    interpreter.string_interner = StringInterner.init(&interpreter.gc);
    interpreter.builtins = try GlobalObject.new(&interpreter.gc);
    interpreter.object_prototype = try ObjectPrototype.new(&interpreter.gc);
    interpreter.string_prototype = try StringPrototype.new(&interpreter.gc);

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

    var ast = Ast.parse(path, buffer[0..file_stats.size :0], i.gc.gpa) catch {
        return Value.None;
    };
    defer ast.deinit();

    var generator = try Generator.init(i.gc.gpa, ast, &i.gc, &i.string_interner);
    const executable = try generator.generate();
    try executable.print();

    const result = i.vm.runExecutable(executable) catch |err| {
        switch (err) {
            error.OutOfMemory => {
                return Value.None;
            },
            error.RegisterPoolExhausted => {
                return Value.None;
            },
            error.ExceptionThrown => {
                std.debug.print("{s}\n", .{try i.exception.?.traceString(i.gc.gpa)});
                return Value.None;
            },
        }
    };

    return result;
}
