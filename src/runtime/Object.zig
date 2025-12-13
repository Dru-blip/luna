const std = @import("std");
const PropertyMap = @import("property_map.zig").PropertyMap;
const Gc = @import("../core/Gc.zig");
const Object = @This();
const ObjectSet = @import("ObjectSet.zig");

marked: bool = false,
ptr: *anyopaque,
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

pub inline fn getPropertyKeyIterator(obj: *Object) PropertyMap.KeyIterator {
    return obj.property_map.keyIterator();
}

pub inline fn getPropertyValueIterator(obj: *Object) PropertyMap.ValueIterator {
    return obj.property_map.valueIterator();
}

pub inline fn getPropertyIterator(obj: *Object) PropertyMap.Iterator {
    return obj.property_map.iterator();
}

pub const Base = struct {
    pub fn visit(self: *Object, live_objects: *ObjectSet) !void {
        //TODO: use a queue instead of recursion.
        try live_objects.add(self);
        var iterator = self.getPropertyIterator();
        while (iterator.next()) |entry| {
            const value = entry.value_ptr.*;
            if (value.isObject()) {
                const object = value.toObject();
                try object.type_descriptor.visit(object, live_objects);
            }
        }
    }

    pub fn finalize(self: *Object, _: *Gc) void {
        self.property_map.deinit();
    }
};

pub fn new(gc: *Gc) *Object {
    const base: *Base = gc.alloc(Base);
    return Object.from(base);
}
