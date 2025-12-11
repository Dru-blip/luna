const PropertyMap = @import("property_map.zig").PropertyMap;
const Gc = @import("../core/Gc.zig");
const Object = @This();

marked: bool = false,
ptr: *anyopaque,
type_descriptor: *const TypeDescriptor,
property_map: PropertyMap,
prototype: ?*Object,

pub const TypeDescriptor = struct {
    name: []const u8,
    finalize: *const fn (*anyopaque, *Gc) void,
    visit: *const fn (*anyopaque, *Gc) void,
};

pub inline fn as(obj: *Object, comptime T: anytype) *T {
    return @ptrCast(@alignCast(obj.ptr));
}

pub const Base = struct {};

pub fn new(gc: *Gc) *Object {
    _ = gc.alloc(Base);
}
