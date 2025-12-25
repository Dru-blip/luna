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

// pub fn set(obj: *Object, key: PropertyKey, value: Value) !void {
//     try obj.property_map.put(key, value);
// }

// pub fn get(_: *Object, _: PropertyKey) ?Value {
//     return null;
// }

pub inline fn isFunction(obj: *Object) bool {
    return obj.type_descriptor == &Function.type_descriptor or obj.type_descriptor == &NativeFunction.type_descriptor;
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

pub const Base = struct {
    pub const type_descriptor: Object.TypeDescriptor = .{
        .name = "Object",
        .visit = visit,
        .finalize = finalize,
    };

    pub fn visit(self: *Object, live_objects: *ObjectSet) !void {
        const class = Object.from(self.class);
        try class.type_descriptor.visit(class, live_objects);
    }

    pub fn finalize(_: *Object, _: *Gc) void {}
};

pub fn new(gc: *Gc) !*Object {
    const obj = try gc.alloc(Base);
    return obj;
}
