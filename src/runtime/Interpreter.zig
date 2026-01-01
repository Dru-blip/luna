const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Ast = @import("../core/Ast.zig");
const StringInterner = @import("StringInterner.zig");
const Vm = @import("Vm.zig");
const Generator = @import("../core/Generator.zig");
const Exception = @import("Exception.zig");
const Object = @import("Object.zig");
const Class = @import("Class.zig");
const StringClass = @import("StringClass.zig");
const DictClass = @import("DictClass.zig");
const ListClass = @import("ListClass.zig");
const BaseClass = @import("BaseClass.zig");
const ListIterator = @import("ListIterator.zig");
const Module = @import("Module.zig");
const Dict = @import("Dict.zig");
const ModuleEnvironment = @import("environments/ModuleEnvironment.zig");

const String = @import("String.zig");

const GlobalObject = @import("GlobalObject.zig");

const Interpreter = @This();

pub const Error = error{ ExceptionThrown, RegisterPoolExhausted } || std.mem.Allocator.Error || error{ ReadFailed, StreamTooLong };

gc: Gc,
gpa: std.mem.Allocator,
generator: ?*Generator = null,
string_interner: StringInterner = undefined,
vm: *Vm = undefined,
exception: ?*Exception = null,
builtins: *Class = undefined,
string_class: *Class = undefined,
base_class: *Class = undefined,
dict_class: *Class = undefined,
list_class: *Class = undefined,
list_iterator_class: *Class = undefined,
common_names: Names = undefined,
module_stack: std.ArrayList(*ModuleEnvironment) = .empty,
module_cache: ModuleCache = undefined,

const ModuleCache = std.StringHashMap(*Module);

pub const Names = struct {
    Class: *String,
    Object: *String,
    Dict: *String,
    List: *String,
    ListIterator: *String,
    String: *String,
    module: *String,
    __getitem__: *String,
    __setitem__: *String,
    __getattr__: *String,
    __setattr__: *String,
    __iter__: *String,
    __next__: *String,
    __init__: *String,
    __str__: *String,
    __add__: *String,
    __radd__: *String,

    pub fn init(string_interner: *StringInterner) !Names {
        return .{
            .Class = try string_interner.intern("Class"),
            .Object = try string_interner.intern("Object"),
            .Dict = try string_interner.intern("Dict"),
            .List = try string_interner.intern("List"),
            .ListIterator = try string_interner.intern("ListIterator"),
            .String = try string_interner.intern("String"),
            .module = try string_interner.intern("<module>"),
            .__getitem__ = try string_interner.intern("__getitem__"),
            .__setitem__ = try string_interner.intern("__setitem__"),
            .__getattr__ = try string_interner.intern("__getattr__"),
            .__setattr__ = try string_interner.intern("__setattr__"),
            .__iter__ = try string_interner.intern("__iter__"),
            .__next__ = try string_interner.intern("__next__"),
            .__str__ = try string_interner.intern("__str__"),
            .__init__ = try string_interner.intern("__init__"),
            .__add__ = try string_interner.intern("__add__"),
            .__radd__ = try string_interner.intern("__radd__"),
        };
    }
};

pub fn init(gpa: std.mem.Allocator) !*Interpreter {
    var interpreter = try gpa.create(Interpreter);
    const gc = Gc.init(gpa, interpreter);

    interpreter.* = .{
        .gc = gc,
        .gpa = gpa,
    };

    interpreter.vm = try Vm.init(gpa, interpreter);
    interpreter.string_interner = StringInterner.init(&interpreter.gc);
    interpreter.base_class = try Class.new(&interpreter.gc);
    interpreter.string_class = try StringClass.new(&interpreter.gc);
    interpreter.dict_class = try DictClass.new(&interpreter.gc);
    interpreter.list_class = try ListClass.new(&interpreter.gc);
    interpreter.list_iterator_class = try ListIterator.new(&interpreter.gc);
    interpreter.builtins = try GlobalObject.new(&interpreter.gc);

    Object.from(interpreter.base_class).class = interpreter.base_class;
    Object.from(interpreter.builtins).class = interpreter.base_class;
    Object.from(interpreter.dict_class).class = interpreter.base_class;
    Object.from(interpreter.list_class).class = interpreter.base_class;
    Object.from(interpreter.list_iterator_class).class = interpreter.base_class;

    try BaseClass.registerMethods(&interpreter.gc, interpreter.base_class);
    try StringClass.registerMethods(&interpreter.gc, interpreter.string_class);
    try DictClass.registerMethods(&interpreter.gc, interpreter.dict_class);
    try ListClass.registerMethods(&interpreter.gc, interpreter.list_class);
    try ListIterator.registerMethods(&interpreter.gc, interpreter.list_iterator_class);

    interpreter.common_names = try Names.init(&interpreter.string_interner);

    interpreter.string_class.name = interpreter.common_names.String;
    interpreter.dict_class.name = interpreter.common_names.Dict;
    interpreter.base_class.name = interpreter.common_names.Class;
    interpreter.list_class.name = interpreter.common_names.List;
    interpreter.list_iterator_class.name = interpreter.common_names.ListIterator;

    interpreter.module_cache = ModuleCache.init(interpreter.gpa);

    return interpreter;
}

pub fn deinit(i: *Interpreter) void {
    i.vm.deinit();
    i.gc.deinit();
    i.gpa.destroy(i);
}

pub fn runFile(i: *Interpreter, path: []const u8) Error!Value {
    var file = std.fs.cwd().openFile(path, .{}) catch {
        //TODO: Handle file opening error
        return i.vm.raiseException(.module_not_found_error, "Module not found: {s}", .{path});
    };

    defer file.close();
    const file_stats = file.stat() catch {
        //TODO: Handle file stat error
        return i.vm.raiseException(.module_not_found_error, "Module not found: {s}", .{path});
    };
    var buffer = try i.gc.gpa.alloc(u8, file_stats.size + 1);

    //TODO: should switch to new reader implementation.
    _ = file.readAll(buffer) catch {
        //TODO: do something with the error
    };
    buffer[file_stats.size] = 0;

    const new_module = try Module.new(&i.gc, path);

    var ast = Ast.parse(new_module.raw_path, buffer[0..file_stats.size :0], i.gc.gpa) catch {
        //TODO: Handle AST parsing error
        return Value.None;
    };
    defer ast.deinit();

    var generator = try Generator.init(i.gc.gpa, ast, &i.gc, &i.string_interner);
    defer generator.deinit();

    const executable = try generator.generate();
    executable.print() catch {};

    const module_env = try ModuleEnvironment.new(&i.gc, new_module, executable.global_variable_count);
    new_module.env = module_env;

    try i.module_stack.append(i.gpa, module_env);
    defer _ = i.module_stack.pop();

    const result = i.vm.runExecutable(executable) catch |err| {
        if (!i.isMainModule()) {
            return err;
        }
        switch (err) {
            error.ExceptionThrown => {
                std.debug.print("{s}\n", .{i.exception.?.traceString(i.gc.gpa) catch ""});
                return Value.None;
            },
            else => {
                return err;
            },
        }
    };

    new_module.exported = result;
    try i.module_cache.put(new_module.raw_path, new_module);
    return result;
}

pub inline fn getRunningModule(i: *Interpreter) *ModuleEnvironment {
    std.debug.assert(i.module_stack.items.len > 0);
    return i.module_stack.items[i.module_stack.items.len - 1];
}

pub inline fn getMainModule(i: *Interpreter) *ModuleEnvironment {
    std.debug.assert(i.module_stack.items.len > 0);
    return i.module_stack.items[0];
}

pub inline fn getGlobalSlots(i: *Interpreter) *Vm.Globals {
    std.debug.assert(i.module_stack.items.len > 0);
    return &i.module_stack.items[i.module_stack.items.len - 1].globals;
}

pub inline fn isMainModule(i: *Interpreter) bool {
    std.debug.assert(i.module_stack.items.len > 0);
    return i.module_stack.items.len == 1;
}

pub inline fn getCachedModule(i: *Interpreter, name: []const u8) ?*Module {
    if (i.module_cache.get(name)) |module| {
        return module;
    }
    return null;
}
