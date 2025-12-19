const std = @import("std");
const PropertyMap = @import("property_map.zig").PropertyMap;
const PropertyKey = @import("property_map.zig").PropertyKey;

const Gc = @import("../core/Gc.zig");
const Object = @This();
const Function = @import("Function.zig");
const NativeFunction = @import("NativeFunction.zig");
const String = @import("String.zig");
const Value = @import("../core/Value.zig");
const ObjectSet = @import("ObjectSet.zig");
const Interpreter = @import("Interpreter.zig");

marked: bool = false,
ptr: *anyopaque, // do i really need this ?
type_descriptor: *const TypeDescriptor,
property_map: PropertyMap,
prototype: ?*Object,

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

pub inline fn getPropertyIterator(obj: *Object) PropertyMap.Iterator {
    return obj.property_map.iterator();
}

pub fn set(obj: *Object, key: PropertyKey, value: Value) !void {
    try obj.property_map.put(key, value);
}

pub fn get(obj: *Object, key: PropertyKey) ?Value {
    return obj.property_map.get(key);
}

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

pub inline fn defineNativeFunction(obj: *Object, interpreter: *Interpreter, name: []const u8, func: NativeFunction.Function, arity: u8, isVariadic: bool) !void {
    const function_name = try interpreter.string_interner.intern(name);
    const function = try NativeFunction.new(&interpreter.gc, func, arity, isVariadic);
    try obj.set(function_name.toPropertyKey(), Value.object(function));
}

pub const Base = struct {
    pub const type_descriptor: Object.TypeDescriptor = .{
        .name = "Object",
        .visit = visit,
        .finalize = finalize,
    };

    pub fn visit(self: *Object, live_objects: *ObjectSet) !void {
        //TODO: find a way to avoid recursion if possible
        try live_objects.add(self);
        var iterator = self.getPropertyIterator();
        while (iterator.next()) |entry| {
            const value = entry.value_ptr.*;
            if (value.isObject()) {
                const object = value.toObject();
                try object.type_descriptor.visit(object, live_objects);
            }
            //TODO: visit property key if it is a string
        }
    }

    pub fn finalize(self: *Object, _: *Gc) void {
        self.property_map.deinit();
    }
};

pub fn new(gc: *Gc) !*Object {
    return try gc.alloc(Base);
}
