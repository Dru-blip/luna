const std = @import("std");

const Gc = @import("../core/Gc.zig");
const Object = @This();
const Function = @import("Function.zig");
const NativeFunction = @import("NativeFunction.zig");
const String = @import("String.zig");
const Value = @import("../core/Value.zig");
const ObjectSet = @import("ObjectSet.zig");
const Interpreter = @import("Interpreter.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");

marked: bool = false,
ptr: *anyopaque, // do i really need this ?
type_descriptor: *const TypeDescriptor,
class: *Class,

pub const TypeDescriptor = struct {
    name: []const u8,
    finalize: *const fn (*Object, *Gc) void,
    visit: *const fn (*Object, *ObjectSet) std.mem.Allocator.Error!void,
};

pub inline fn as(obj: *Object, comptime T: anytype) *T {
    return @ptrCast(@alignCast(obj.ptr));
}

pub inline fn from(ptr: *anyopaque) *Object {
    const obj_base = @intFromPtr(ptr);
    return @ptrFromInt(obj_base - @sizeOf(Object));
}

pub inline fn isFunction(obj: *Object) bool {
    //TODO: should switch to class based
    return obj.type_descriptor == &Function.type_descriptor or
        obj.type_descriptor == &NativeFunction.type_descriptor or
        obj.type_descriptor == &Class.type_descriptor;
}

pub inline fn isString(obj: *Object) bool {
    return obj.type_descriptor == &String.type_descriptor;
}

pub inline fn toString(obj: *Object) *String {
    return obj.as(String);
}

pub inline fn asString(obj: *Object) ?*String {
    if (obj.type_descriptor == &String.type_descriptor) {
        return obj.as(String);
    }
    return null;
}

pub inline fn asFunction(obj: *Object) ?*Function {
    return if (obj.type_descriptor == &Function.type_descriptor) obj.as(Function) else null;
}

pub inline fn asNativeFunction(obj: *Object) ?*NativeFunction {
    return if (obj.type_descriptor == &NativeFunction.type_descriptor) obj.as(NativeFunction) else null;
}

pub inline fn asClass(obj: *Object) ?*Class {
    return if (obj.type_descriptor == &Class.type_descriptor) obj.as(Class) else null;
}

pub inline fn getClassName(obj: *Object) []const u8 {
    return obj.class.name.asSlice();
}

pub const Base = struct {
    pub const type_descriptor: Object.TypeDescriptor = .{
        .name = "Object",
        .visit = visit,
        .finalize = finalize,
    };

    pub fn visit(self: *Object, live_objects: *ObjectSet) !void {
        if (live_objects.contains(self)) return;
        try live_objects.add(self);
        const class = Object.from(self.class);
        try class.type_descriptor.visit(class, live_objects);
    }

    pub fn finalize(_: *Object, _: *Gc) void {}
};

pub fn new(gc: *Gc) !*Object {
    const obj = try gc.alloc(Base);
    return obj;
}

pub fn callAssumeCallable(
    self: *Object,
    vm: *Vm,
    this_value: *Object,
    args: []const Value,
) !Value {
    if (self.asNativeFunction()) |native_fn| {
        return native_fn.function(vm, this_value, args);
    }

    if (self.asFunction()) |function| {
        _ = try vm.checkArity(Function, function.exe.name, function.arity, @intCast(args.len), false);

        vm.records.appendAssumeCapacity(
            try Vm.ActivationRecord.withFunction(&vm.register_pool, function),
        );

        var record = &vm.records.items[vm.records.items.len - 1];
        record.caller_return_reg = 0;

        record.registers[0] = Value.object(this_value);
        for (args, 0..) |arg, i| {
            record.registers[i + 1] = arg;
        }

        return vm.runRecord(record, true);
    }

    return vm.raiseException(
        .type_error,
        "{s} object is not callable",
        .{self.getClassName()},
    );
}
