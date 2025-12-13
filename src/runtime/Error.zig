const std = @import("std");
const String = @import("String.zig");
const Loc = @import("../core/Tokenizer.zig").Token.Loc;
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Gc = @import("../core/Gc.zig");

const Error = @This();

pub const Tag = enum {
    type_error,
    reference_error,
};

pub const TracebackFrame = struct {
    function_name: *String,
    file_path: []const u8,
    loc: Loc,
};

message: *String,
traceback: std.ArrayList(TracebackFrame),

pub const type_descriptor: Object.TypeDescriptor = .{
    .name = "Error",
    .visit = visit,
    .finalize = finalize,
};

fn finalize(self: *Object, gc: *Gc) void {
    const err: *Error = self.as(Error);
    err.traceback.deinit(gc.gpa);
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const err: *Error = self.as(Error);
    const str_obj = Object.from(err.message);
    try str_obj.type_descriptor.visit(str_obj, live_objects);
}
