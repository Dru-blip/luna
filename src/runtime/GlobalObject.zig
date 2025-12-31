const std = @import("std");
const Object = @import("Object.zig");
const Gc = @import("../core/Gc.zig");
const ConsoleObject = @import("ConsoleObject.zig");
const Value = @import("../core/Value.zig");
const Vm = @import("Vm.zig");
const Interpreter = @import("Interpreter.zig");
const Class = @import("Class.zig");
const String = @import("String.zig");
const Module = @import("Module.zig");

pub fn new(gc: *Gc) !*Class {
    const class = try Class.new(gc);

    try class.defineNativeMethod(gc, "print", print, 10, true);
    try class.defineNativeMethod(gc, "input", input, 0, true);
    try class.defineNativeMethod(gc, "import", import, 1, false);

    return class;
}

fn input(vm: *Vm, _: *Object, args: []const Value) !Value {
    if (args.len > 0) {
        if (args[0].isString()) {
            const prompt = args[0].toObject().toString();
            _ = std.fs.File.stdout().write(prompt.asSlice()) catch {};
        }
    }
    var input_buffer: [1024]u8 = undefined;
    var input_reader = std.fs.File.stdin().reader(&input_buffer);
    const input_str = try input_reader.interface.takeDelimiter('\n');
    if (input_str) |str| {
        return Value.object(try String.new(vm.gc, str));
    }
    return Value.object(try String.new(vm.gc, ""));
}

fn print(vm: *Vm, _: *Object, args: []const Value) !Value {
    for (args) |arg| {
        switch (arg.type) {
            .number => {
                std.debug.print("{any} ", .{arg.data.number});
            },
            .bool => {
                std.debug.print("{s} ", .{if (arg.data.bool) "true" else "false"});
            },
            .none => {
                std.debug.print("none", .{});
            },
            .undefined => {
                std.debug.print("undefined", .{});
            },
            .object => {
                if (arg.toObject().asString()) |str| {
                    std.debug.print("{s} ", .{str.asSlice()});
                    continue;
                }
                const class = arg.toObject().class;
                const str_method_value = class.getField(try vm.interpreter.string_interner.intern("__str__"));
                if (str_method_value) |str_method_object| {
                    if (str_method_object.asObject()) |str| {
                        const s = try str.callAssumeCallable(vm, arg.toObject(), &[_]Value{});
                        //TODO: should check if s is not a string
                        // if (s.type != .string) {
                        //     throw Error("TypeError", "Expected string, got {}", .{s.type});
                        // }
                        std.debug.print("{s} ", .{s.toObject().asString().?.asSlice()});
                        continue;
                    }
                    std.debug.print("Object", .{});
                } else {
                    std.debug.print("Object", .{});
                }
            },
        }
    }
    std.debug.print("\n", .{});
    return Value.None;
}

fn import(vm: *Vm, _: *Object, args: []const Value) Interpreter.Error!Value {
    const path_value = args[0];
    if (!path_value.isString()) {
        //TODO: throw error
        // return Error("TypeError", "Expected string, got {}", .{path_value.type});
    }
    const path_string = path_value.toObject().toString();

    if (try getCachedModuleWithPatterns(vm, path_string.asSlice())) |module| {
        // std.debug.print("Module Cache hit :{s}\n", .{path_string.asSlice()});
        return module.exported;
    }

    const path = resolveModulePath(vm, path_string.asSlice()) catch {
        return vm.raiseException(.module_not_found_error, "'{s}'", .{path_string.asSlice()});
    };

    defer vm.gpa.free(path);

    return try vm.interpreter.runFile(path);
}

inline fn resolveModulePath(vm: *Vm, module_path: []const u8) ![]u8 {
    var path_buffer = try vm.gpa.alloc(u8, module_path.len);
    defer vm.gpa.free(path_buffer);

    for (module_path, 0..) |c, i| {
        path_buffer[i] = if (c == '.') std.fs.path.sep else c;
    }

    const patterns = [_][]const u8{
        ".luna",
        "/__init__.luna",
        "/index.luna",
    };

    for (patterns) |pattern| {
        const candidate = try std.mem.concat(vm.gpa, u8, &[_][]const u8{ path_buffer, pattern });
        errdefer vm.gpa.free(candidate);

        std.fs.cwd().access(candidate, .{}) catch {
            vm.gpa.free(candidate);
            continue;
        };

        return candidate;
    }

    return error.ModuleNotFound;
}

fn getCachedModuleWithPatterns(vm: *Vm, module_path: []const u8) !?*Module {
    var path_buffer = try vm.gpa.alloc(u8, module_path.len);
    defer vm.gpa.free(path_buffer);

    for (module_path, 0..) |c, i| {
        path_buffer[i] = if (c == '.') std.fs.path.sep else c;
    }

    const patterns = [_][]const u8{
        ".luna",
        "/__init__.luna",
        "/index.luna",
    };

    for (patterns) |pattern| {
        const candidate = try std.mem.concat(vm.gpa, u8, &[_][]const u8{ path_buffer, pattern });
        defer vm.gpa.free(candidate);

        if (vm.interpreter.getCachedModule(candidate)) |module| {
            return module;
        }
    }

    return null;
}
