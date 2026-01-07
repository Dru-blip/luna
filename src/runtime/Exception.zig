const std = @import("std");
const String = @import("String.zig");
const Loc = @import("../core/Tokenizer.zig").Token.Loc;
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Gc = @import("../core/Gc.zig");
const Vm = @import("Vm.zig");
const Class = @import("Class.zig");

const Exception = @This();

const SourceCache = struct {
    map: std.StringHashMap([]const u8),
    gpa: std.mem.Allocator,

    fn init(gpa: std.mem.Allocator) SourceCache {
        return .{
            .map = std.StringHashMap([]const u8).init(gpa),
            .gpa = gpa,
        };
    }

    fn deinit(self: *SourceCache) void {
        var iter = self.map.valueIterator();
        while (iter.next()) |source| {
            self.gpa.free(source.*);
        }
        self.map.deinit();
    }

    fn getOrLoad(self: *SourceCache, file_path: []const u8) ![]const u8 {
        if (self.map.get(file_path)) |source| {
            return source;
        }

        const source = try readFile(file_path, self.gpa);
        try self.map.put(file_path, source);
        return source;
    }
};

pub const TracebackFrame = struct {
    function_name: *String,
    file_path: []const u8,
    location: Loc,
};

message: *String = undefined,
traceback: std.ArrayList(TracebackFrame) = .empty,

pub const gc_hooks: Object.GcHooks = .{
    .name = "Exception",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(
    gc: *Gc,
    class: *Class,
    message: *String,
) !*Exception {
    const obj = try gc.alloc(Exception);
    obj.class = class;

    const ex: *Exception = obj.as(Exception);
    ex.* = .{
        .message = message,
        .traceback = .empty,
    };
    return ex;
}

pub fn withMessage(vm: *Vm, exception_class: *Class, comptime fmt: []const u8, args: anytype) !*Exception {
    const obj = try vm.gc.alloc(Exception);
    obj.class = exception_class;
    const ex: *Exception = obj.as(Exception);
    const raw_msg = try std.fmt.allocPrint(vm.gpa, fmt, args);
    defer vm.gpa.free(raw_msg);
    const message_obj = try String.new(vm.gc, raw_msg);
    ex.* = .{
        .message = message_obj.as(String),
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

pub fn traceString(exception: *Exception, gpa: std.mem.Allocator) ![]const u8 {
    var source_cache = SourceCache.init(gpa);
    defer source_cache.deinit();
    var buffer: std.ArrayList(u8) = .empty;
    var writer = buffer.writer(gpa);
    try writer.print("Traceback (most recent call last):\n", .{});

    for (exception.traceback.items) |*frame| {
        var source: []const u8 = try source_cache.getOrLoad(frame.file_path);
        try writer.print("   at {s} ({s}:{d}:{d})\n", .{ frame.function_name.asSlice(), frame.file_path, frame.location.line, frame.location.col });

        var line_start_offset: usize = 0;
        var line_length: usize = 0;
        //TODO: format if it spans multiple lines.
        extractSourceLine(source, &frame.location, &line_start_offset, &line_length);
        try writer.print("\t{s}\n\t", .{source[line_start_offset .. line_start_offset + line_length]});
        for (0..(frame.location.start - line_start_offset)) |_| {
            try writer.print(" ", .{});
        }
        for (0..(frame.location.end - frame.location.start)) |_| {
            try writer.print("^", .{});
        }
        try writer.print("\n", .{});
    }

    const class = Object.from(exception).class;
    try writer.print("{s}: {s}", .{ class.name.asSlice(), exception.message.asSlice() });
    return try buffer.toOwnedSlice(gpa);
}

fn finalize(self: *Object, gc: *Gc) void {
    const err: *Exception = self.as(Exception);
    err.traceback.deinit(gc.gpa);
    Object.Base.finalize(self, gc);
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const err: *Exception = self.as(Exception);
    const message = Object.from(err.message);
    if (!live_objects.contains(message)) {
        try message.gc_hooks.visit(message, live_objects);
    }

    for (err.traceback.items) |frame| {
        const func_name_obj = Object.from(frame.function_name);
        if (!live_objects.contains(func_name_obj)) {
            try func_name_obj.gc_hooks.visit(func_name_obj, live_objects);
        }
    }
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

fn extractSourceLine(
    source: []const u8,
    loc: *const Loc,
    line_start_offset: *usize,
    line_length: *usize,
) void {
    var start = loc.start;
    var end = loc.end;
    while (start > 0 and source[start - 1] != '\n')
        start -= 1;
    while (end < source.len and source[end] != '\n')
        end += 1;
    line_start_offset.* = start;
    line_length.* = end - start;
}
