const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const String = @import("String.zig");

pub const Range = struct {
    const Self = @This();

    start: f64,
    end: f64,
    step: f64,
    current: f64,

    pub const gc_hooks = Object.GcHooks{
        .name = "Range",
        .visit = visit,
        .finalize = finalize,
    };

    pub fn new(gc: *Gc, start: f64, end: f64) !*Object {
        const obj = try gc.alloc(Self);
        obj.class = gc.interpreter.range_class;
        var range: *Self = obj.as(Self);
        range.start = start;
        range.end = end;
        range.step = 1;
        range.current = start;
        return obj;
    }

    fn visit(self: *Object, live_objects: *ObjectSet) !void {
        try Object.Base.visit(self, live_objects);
    }

    fn finalize(_: *Object, _: *Gc) void {}
};

pub const RangeClass = struct {
    pub fn new(gc: *Gc) !*Class {
        const rc = try Class.new(gc);
        rc.constructor = constructor;
        return rc;
    }

    pub fn registerMethods(gc: *Gc, rc: *Class) !void {
        try rc.defineNativeMethod(gc, "__constructor__", constructor, 3, true);
        try rc.defineNativeMethod(gc, "__iter__", iter, 0, false);
        try rc.defineNativeMethod(gc, "__str__", str, 0, false);
    }

    fn constructor(vm: *Vm, _: *Object, args: []const Value) !Value {
        if (args.len < 2) {
            return vm.raiseValueError("Range requires at least 2 arguments (start, end)", .{});
        }

        const start_value = args[0];
        const end_value = args[1];

        if (start_value.type != .number) {
            return vm.raiseTypeError("Range start must be a number", .{});
        }
        if (end_value.type != .number) {
            return vm.raiseTypeError("Range end must be a number", .{});
        }

        const start = start_value.data.number;
        const end = end_value.data.number;

        const range = try Range.new(vm.gc, start, end);
        return Value.object(range);
    }

    fn iter(vm: *Vm, self: *Object, _: []const Value) !Value {
        const iterator = try RangeIterator.newInstance(vm.gc, self);
        return Value.object(iterator);
    }

    fn str(vm: *Vm, self: *Object, _: []const Value) !Value {
        const range: *Range = self.as(Range);

        var buffer: [120]u8 = undefined;
        const str_buffer = std.fmt.bufPrint(
            &buffer,
            "Range({d}, {d}, {d})",
            .{ range.start, range.end, range.step },
        ) catch "Range()";
        return Value.object(try String.new(vm.gc, str_buffer));
    }
};

pub const RangeIterator = struct {
    range: *Object,
    current: f64,

    pub const gc_hooks = Object.GcHooks{
        .name = "RangeIterator",
        .visit = visit,
        .finalize = finalize,
    };

    pub fn new(gc: *Gc) !*Class {
        const ic = try Class.new(gc);
        ic.super_class = gc.interpreter.base_class;
        return ic;
    }

    pub fn newInstance(gc: *Gc, range_obj: *Object) !*Object {
        const obj = try gc.alloc(RangeIterator);
        obj.class = gc.interpreter.range_iterator_class;
        var iterator: *RangeIterator = obj.as(RangeIterator);
        iterator.range = range_obj;
        const range_data: *Range = range_obj.as(Range);
        iterator.current = range_data.start;
        return obj;
    }

    fn visit(self: *Object, live_objects: *ObjectSet) !void {
        try Object.Base.visit(self, live_objects);
        const iterator: *RangeIterator = self.as(RangeIterator);

        if (!live_objects.contains(iterator.range)) {
            try iterator.range.gc_hooks.visit(iterator.range, live_objects);
        }
    }

    fn finalize(_: *Object, _: *Gc) void {}

    pub fn registerMethods(gc: *Gc, ic: *Class) !void {
        try ic.defineNativeMethod(gc, "__next__", next, 0, false);
    }

    fn next(_: *Vm, self: *Object, _: []const Value) !Value {
        const iterator: *RangeIterator = self.as(RangeIterator);
        const range: *Range = iterator.range.as(Range);

        if (iterator.current >= range.end) {
            return Value.Undefined;
        }

        const result = iterator.current;
        iterator.current += range.step;
        return Value.number(result);
    }
};
