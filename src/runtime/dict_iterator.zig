const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Class = @import("Class.zig");
const Vm = @import("Vm.zig");
const Dict = @import("Dict.zig");
const String = @import("String.zig");

pub fn DictIterator(comptime mode: Dict.Mode, comptime name: []const u8) type {
    const Iterator = Dict.Iterator(mode);
    return struct {
        const Self = @This();

        dict: *Object,
        iterator: Iterator,

        pub const gc_hooks = Object.GcHooks{
            .name = name,
            .visit = visit,
            .finalize = finalize,
        };

        pub fn new(gc: *Gc) !*Class {
            const ic = try Class.new(gc);
            //TODO: Should switch to base iterator class
            ic.super_class = gc.interpreter.base_class;
            return ic;
        }

        pub fn newInstance(gc: *Gc, dict_obj: *Object) !*Object {
            const obj = try gc.alloc(Self);
            obj.class = getClassByMode(gc);

            const self: *Self = obj.as(Self);
            self.dict = dict_obj;
            self.iterator = Iterator{ .map = &dict_obj.as(Dict).map };
            return obj;
        }

        pub fn registerMethods(gc: *Gc, ic: *Class) !void {
            try ic.defineNativeMethod(gc, "__iter__", iter, 0, false);
            try ic.defineNativeMethod(gc, "__next__", next, 0, false);
        }

        fn iter(_: *Vm, self_obj: *Object, _: []const Value) !Value {
            return Value.object(self_obj);
        }

        fn next(_: *Vm, self_obj: *Object, _: []const Value) !Value {
            const self: *Self = self_obj.as(Self);

            const item = self.iterator.next() orelse
                return Value.Undefined;

            return switch (mode) {
                .Key => item,
                .Value => item,
                .Entry => item.key,
            };
        }

        fn visit(self_obj: *Object, live: *ObjectSet) !void {
            try Object.Base.visit(self_obj, live);
            const self: *Self = self_obj.as(Self);

            if (!live.contains(self.dict)) {
                try self.dict.gc_hooks.visit(self.dict, live);
            }
        }

        fn finalize(_: *Object, _: *Gc) void {}

        fn getClassByMode(gc: *Gc) *Class {
            return switch (mode) {
                .Key => gc.interpreter.dict_key_iterator_class,
                .Value => gc.interpreter.dict_value_iterator_class,
                .Entry => gc.interpreter.dict_entry_iterator_class,
            };
        }
    };
}

pub const DictKeyIterator = DictIterator(.Key, "DictKeyIterator");
pub const DictValueIterator = DictIterator(.Value, "DictValueIterator");
pub const DictEntryIterator = DictIterator(.Entry, "DictEntryIterator");
