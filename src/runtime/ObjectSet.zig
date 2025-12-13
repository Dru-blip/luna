const std = @import("std");
const Object = @import("Object.zig");

const ObjectSet = @This();
const Map = std.AutoHashMap(*Object, void);

objects: Map,

pub fn init(gpa: std.mem.Allocator) ObjectSet {
    return .{
        .objects = Map.init(gpa),
    };
}

pub inline fn add(self: *ObjectSet, obj: *Object) std.mem.Allocator.Error!void {
    try self.objects.put(obj, {});
}

pub inline fn iterator(self: *ObjectSet) Map.KeyIterator {
    return self.objects.keyIterator();
}

pub inline fn size(self: *ObjectSet) usize {
    return self.objects.count();
}
