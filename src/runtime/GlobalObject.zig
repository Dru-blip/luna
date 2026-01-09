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
    try class.defineNativeMethod(gc, "hash", hash, 1, false);
    try class.defineNativeMethod(gc, "len", len, 1, false);

    try class.putField(gc.interpreter.common_names.List, Value.object(Object.from(gc.interpreter.list_class)));
    try class.putField(gc.interpreter.common_names.Dict, Value.object(Object.from(gc.interpreter.dict_class)));

    try class.putField(gc.interpreter.common_names.BaseException, Value.object(Object.from(gc.interpreter.base_exception_class)));

    try class.putField(gc.interpreter.common_names.TypeError, Value.object(Object.from(gc.interpreter.type_error_class)));
    try class.putField(gc.interpreter.common_names.ReferenceError, Value.object(Object.from(gc.interpreter.reference_error_class)));
    try class.putField(gc.interpreter.common_names.ZeroDivisionError, Value.object(Object.from(gc.interpreter.zero_division_error_class)));
    try class.putField(gc.interpreter.common_names.IndexError, Value.object(Object.from(gc.interpreter.index_error_class)));
    try class.putField(gc.interpreter.common_names.ValueError, Value.object(Object.from(gc.interpreter.value_error_class)));
    try class.putField(gc.interpreter.common_names.AttributeError, Value.object(Object.from(gc.interpreter.attribute_error_class)));
    try class.putField(gc.interpreter.common_names.ModuleNotFoundError, Value.object(Object.from(gc.interpreter.module_not_found_error_class)));
    try class.putField(gc.interpreter.common_names.InvalidAssignmentTargetError, Value.object(Object.from(gc.interpreter.invalid_assignment_target_error_class)));
    try class.putField(gc.interpreter.common_names.StackOverflowError, Value.object(Object.from(gc.interpreter.stack_overflow_error_class)));

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
    //TODO: switch to writer instead of default debug print.
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
                const str_method_value = class.getField(vm.interpreter.common_names.__str__);
                if (str_method_value) |str_method_object| {
                    if (str_method_object.asObject()) |str_method| {
                        const s = try str_method.callAssumeCallable(vm, arg.toObject(), &[_]Value{});
                        if (s.asObject()) |obj| {
                            if (obj.asString()) |str| {
                                std.debug.print("{s} ", .{str.asSlice()});
                                continue;
                            }
                        }
                        return vm.raiseTypeError("__str__ return a non string object", .{});
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
        return vm.raiseModuleNotFoundError("'{s}'", .{path_string.asSlice()});
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

fn hash(vm: *Vm, _: *Object, args: []const Value) !Value {
    if (args.len == 0) {
        return vm.raiseTypeError("hash() requires at least 1 argument", .{});
    }

    const value = args[0];
    var hash_value: u64 = undefined;

    switch (value.type) {
        .number => {
            const num_bits = @as(u64, @bitCast(value.data.number));
            hash_value = std.hash.Wyhash.hash(0, std.mem.asBytes(&num_bits));
        },
        .bool => {
            const bool_val: u8 = if (value.data.bool) 1 else 0;
            hash_value = std.hash.Wyhash.hash(0, &[_]u8{bool_val});
        },
        .none => {
            hash_value = 0;
        },
        .undefined => {
            hash_value = 1;
        },
        .object => {
            if (value.toObject().asString()) |str| {
                hash_value = str.hash;
            } else {
                const class = value.toObject().class;
                const hash_method_value = class.getField(vm.interpreter.common_names.__hash__);

                if (hash_method_value) |hash_method_object| {
                    if (hash_method_object.asObject()) |hash_obj| {
                        const result = try hash_obj.callAssumeCallable(vm, value.toObject(), &[_]Value{});
                        if (result.type != .number) {
                            return vm.raiseTypeError("__hash__ must return a number", .{});
                        }
                        return result;
                    }
                }
                return vm.raiseTypeError("unhashable type: {s}", .{class.name.asSlice()});
            }
        },
    }

    const hash_as_f64 = @as(f64, @floatFromInt(hash_value));
    return Value.number(hash_as_f64);
}

fn len(vm: *Vm, _: *Object, args: []const Value) !Value {
    const value = args[0];
    if (value.asObject()) |obj| {
        if (obj.asString()) |str| {
            return Value.number(@as(f64, @floatFromInt(str.length)));
        }

        const class = obj.class;
        if (class.getField(vm.interpreter.common_names.__len__)) |len_val| {
            if (len_val.asObject()) |len_obj| {
                const result = try len_obj.callAssumeCallable(vm, obj, &[_]Value{});
                if (result.type != .number) {
                    return vm.raiseTypeError("__len__ must return a number", .{});
                }
                return result;
            }
            //TODO: raise error if __len__ is not a function
        }
    }

    return vm.raiseTypeError("type '{s}' does not support len()", .{value.getTypeString()});
}
