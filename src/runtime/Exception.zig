const std = @import("std");
const String = @import("String.zig");
const Loc = @import("../core/Tokenizer.zig").Token.Loc;
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Gc = @import("../core/Gc.zig");
const Vm = @import("Vm.zig");

const Exception = @This();

pub const Tag = enum {
    type_error,
    reference_error,
};

pub const TracebackFrame = struct {
    function_name: *String,
    file_path: []const u8,
    location: Loc,
};

tag: Tag,
message: *String = undefined,
traceback: std.ArrayList(TracebackFrame) = .empty,

pub const type_descriptor: Object.TypeDescriptor = .{
    .name = "Exception",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc) !*Exception {
    const ex = try gc.alloc(Exception);
    return ex;
}

pub fn withMessage(vm: *Vm, tag: Tag, comptime fmt: []const u8, args: anytype) !*Exception {
    const ex: *Exception = try vm.gc.alloc(Exception);
    const raw_msg = try std.fmt.allocPrint(vm.gc.gpa, fmt, args);
    defer vm.gc.gpa.free(raw_msg);
    ex.* = .{
        .tag = tag,
        .message = try String.new(vm.gc, raw_msg),
    };
    try ex.buildTraceback(vm);
    return ex;
}

pub fn buildTraceback(exception: *Exception, vm: *Vm) !void {
    for (vm.records.items) |*record| {
        const traceback_frame: TracebackFrame = .{
            .file_path = record.executable.filepath,
            .function_name = record.executable.name,
            .location = record.executable.spans[record.ip - 1],
        };

        try exception.traceback.append(vm.gpa, traceback_frame);
    }
}

fn finalize(self: *Object, gc: *Gc) void {
    const err: *Exception = self.as(Exception);
    err.traceback.deinit(gc.gpa);
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const err: *Exception = self.as(Exception);
    const str_obj = Object.from(err.message);
    try str_obj.type_descriptor.visit(str_obj, live_objects);
}

fn readFile(path: []const u8, gpa: std.mem.Allocator) ![]const u8 {
    var file = try std.fs.cwd().openFile(path, .{});
    defer file.close();
    const file_stats = try file.stat();
    var buffer = try gpa.alloc(u8, file_stats.size + 1);
    _ = try file.readAll(buffer);
    buffer[file_stats.size] = 0;
    return buffer;
}
