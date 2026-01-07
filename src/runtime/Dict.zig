const std = @import("std");
const Gc = @import("../core/Gc.zig");
const Value = @import("../core/Value.zig");
const Object = @import("Object.zig");
const ObjectSet = @import("ObjectSet.zig");
const Class = @import("Class.zig");
const String = @import("String.zig");
const Vm = @import("Vm.zig");

const Dict = @This();

const Map = HashMap;

map: Map,

pub const gc_hooks = Object.GcHooks{
    .name = "Dict",
    .visit = visit,
    .finalize = finalize,
};

pub fn new(gc: *Gc) !*Object {
    const obj = try gc.alloc(Dict);
    obj.class = gc.interpreter.dict_class;
    var dict: *Dict = obj.as(Dict);

    dict.map = Map.init(gc.gpa);
    return obj;
}

pub fn set(dict: *Dict, vm: *Vm, key: Value, value: Value) !void {
    try dict.map.put(vm, key, value);
}

pub fn get(dict: *Dict, vm: *Vm, key: Value) !?Value {
    return try dict.map.get(vm, key);
}

pub fn remove(dict: *Dict, vm: *Vm, key: Value) !?Value {
    return try dict.map.remove(vm, key);
}

pub fn contains(dict: *Dict, vm: *Vm, key: Value) !bool {
    return try dict.map.contains(vm, key);
}

// pub fn clear(dict: *Dict) void {
//     dict.map.clearAndFree();
// }

pub fn size(dict: *Dict) usize {
    return dict.map.count;
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    const dict: *Dict = self.as(Dict);

    var iterator = dict.map.iterator();
    while (iterator.next()) |entry| {
        if (entry.key.asObject()) |key_obj| {
            if (!live_objects.contains(key_obj)) {
                try key_obj.gc_hooks.visit(key_obj, live_objects);
            }
        }
        if (entry.value.asObject()) |value_obj| {
            if (!live_objects.contains(value_obj)) {
                try value_obj.gc_hooks.visit(value_obj, live_objects);
            }
        }
    }
}

fn finalize(self: *Object, _: *Gc) void {
    const dict: *Dict = self.as(Dict);
    dict.map.deinit();
    // Object.Base.finalize(self, gc);
}

pub const HashMap = struct {
    const Self = @This();

    allocator: std.mem.Allocator,
    indices: []u32,
    count: usize = 0,
    buckets: std.ArrayList(Entry) = .empty,
    capacity: usize = 0,
    free_list: u32 = Entry.empty,
    free_count: usize = 0,

    const Entry = struct {
        key: Value,
        value: Value,
        next: u32,
        prev: u32,
        free: bool = false,

        const empty: u32 = std.math.maxInt(u32);
    };

    pub const Iterator = struct {
        map: *const Self,
        index: usize = 0,

        pub fn next(self: *Iterator) ?Entry {
            while (self.index < self.map.buckets.items.len) {
                const current_index = self.index;
                self.index += 1;

                const entry = self.map.buckets.items[current_index];
                if (!entry.free) {
                    return entry;
                }
            }
            return null;
        }
    };

    pub fn init(allocator: std.mem.Allocator) Self {
        return .{
            .allocator = allocator,
            .indices = &.{},
        };
    }

    pub fn deinit(self: *Self) void {
        self.count = 0;
        self.capacity = 0;
        self.allocator.free(self.indices);
        self.buckets.deinit(self.allocator);
    }

    fn hash(_: *const Self, vm: *Vm, key: Value) !Value {
        switch (key.type) {
            .number => {
                const bits = @as(u64, @bitCast(key.data.number));
                return Value.number(@floatFromInt(std.hash.Wyhash.hash(0, std.mem.asBytes(&bits))));
            },
            .bool => return Value.number(@floatFromInt(@intFromBool(key.data.bool))),
            .none => return Value.number(0),
            .undefined => return Value.number(1),
            .object => {
                const obj = key.toObject();

                if (obj.asString()) |str| {
                    return Value.number(@floatFromInt(str.hash));
                }

                const class = obj.class;
                if (class.getField(vm.interpreter.common_names.__hash__)) |method| {
                    if (method.asObject()) |func| {
                        const hash_value = try func.callAssumeCallable(vm, obj, &[_]Value{});
                        if (hash_value.isNumber()) {
                            return hash_value;
                        }
                        return vm.raiseException(.value_error, "__hash__ must return a number", .{});
                    }
                }

                return vm.raiseException(.type_error, "unhashable type: {s}", .{class.name.asSlice()});
            },
        }
    }

    inline fn hashIndex(self: *const Self, vm: *Vm, key: Value) !u64 {
        const h = try self.hash(vm, key);
        const val = @as(u64, @intFromFloat(h.data.number));
        return @intCast(val & (self.indices.len - 1));
    }

    fn eql(_: *Self, vm: *Vm, a: Value, b: Value) !Value {
        if (a.type != b.type) return Value.bool(false);
        switch (a.type) {
            .number => return Value.bool(a.data.number == b.data.number),
            .bool => return Value.bool(a.data.bool == b.data.bool),
            .none, .undefined => return Value.bool(true),
            .object => {
                const ao = a.toObject();
                const bo = b.toObject();
                if (ao == bo) return Value.bool(true);
                if (ao.asString()) |as| {
                    if (bo.asString()) |bs| {
                        return Value.bool(as.eql(bs));
                    }
                    return Value.bool(false);
                }
                const aclass = ao.class;
                if (aclass.getField(vm.interpreter.common_names.__eq__)) |method| {
                    if (method.asObject()) |func| {
                        const hash_value = try func.callAssumeCallable(vm, ao, &[_]Value{b});
                        if (hash_value.isBool()) {
                            return hash_value;
                        }
                        return vm.raiseException(.value_error, "__eq__ must return a boolean", .{});
                    }
                }

                return Value.bool(false);
            },
        }
    }

    pub fn iterator(self: *Self) Iterator {
        return .{
            .map = self,
        };
    }

    pub fn get(self: *Self, vm: *Vm, key: Value) !?Value {
        const index = try self.getIndex(vm, key) orelse return null;
        return self.buckets.items[index].value;
    }

    pub fn getPtr(self: *Self, vm: *Vm, key: Value) !?*Value {
        const entry_index = try self.getIndex(vm, key) orelse return null;
        return &self.buckets.items[entry_index].value;
    }

    pub fn contains(self: *Self, vm: *Vm, key: Value) !bool {
        _ = try self.getIndex(vm, key) orelse return false;
        return true;
    }

    pub fn put(self: *Self, vm: *Vm, key: Value, value: Value) !void {
        try self.growIfNeeded(vm, 1);
        try self.putAssumeCapacity(vm, key, value);
    }

    pub fn remove(self: *Self, vm: *Vm, key: Value) !?Value {
        const index = try self.getIndex(vm, key) orelse return null;
        const entry = &self.buckets.items[index];

        if (self.free_list == Entry.empty) {
            self.free_list = index;
        } else {
            const next = self.free_list;
            self.free_list = index;
            self.buckets.items[index].next = next;
        }
        entry.free = true;
        self.free_count += 1;
        return entry.value;
    }

    pub fn putAssumeCapacity(self: *Self, vm: *Vm, key: Value, value: Value) !void {
        if (try self.getIndex(vm, key)) |entry_index| {
            self.buckets.items[entry_index].value = value;
            return;
        }

        //TODO: recalculating the hash is a waste of time , just retrieve the hash generated from getIndex call.
        const index = try self.hashIndex(vm, key);

        const new_bucket_index: u32 = if (self.free_list != Entry.empty) blk: {
            const idx = self.free_list;
            const entry = &self.buckets.items[idx];
            self.free_list = entry.next;
            self.free_count -= 1;
            break :blk idx;
        } else blk: {
            const idx: u32 = @intCast(self.buckets.items.len);
            self.buckets.appendAssumeCapacity(Entry{
                .key = key,
                .value = value,
                .next = Entry.empty,
                .prev = Entry.empty,
            });
            break :blk idx;
        };

        const new_entry = &self.buckets.items[new_bucket_index];
        new_entry.key = key;
        new_entry.value = value;
        new_entry.next = self.indices[index];
        new_entry.free = false;
        new_entry.prev = Entry.empty;

        if (self.indices[index] != Entry.empty) {
            self.buckets.items[self.indices[index]].prev = new_bucket_index;
        }

        self.indices[index] = new_bucket_index;
        self.count += 1;
    }

    pub fn growIfNeeded(self: *Self, vm: *Vm, additional: usize) !void {
        if (self.free_count >= additional) return;
        const target_count = self.count + additional;
        const capacity = self.indices.len;

        //TODO: find a better way to calculate the load factor.
        const load = @as(f64, @floatFromInt(target_count)) / @as(f64, @floatFromInt(capacity));

        if (load >= 0.75 or self.capacity == 0) {
            const new_capacity =
                std.math.ceilPowerOfTwo(usize, @max(4, target_count)) catch return error.OutOfMemory;

            try self.grow(vm, new_capacity);
        }
    }

    pub fn getIndex(self: *Self, vm: *Vm, key: Value) !?u32 {
        const index = try self.hashIndex(vm, key);
        var entry_index = self.indices[index];
        while (entry_index != Entry.empty) {
            const entry = &self.buckets.items[entry_index];
            if ((try self.eql(vm, entry.key, key)).data.bool) {
                return entry_index;
            }
            entry_index = entry.next;
        }

        return null;
    }

    pub fn grow(self: *Self, vm: *Vm, new_capacity: usize) !void {
        const new_indices = try self.allocator.alloc(u32, new_capacity);
        @memset(new_indices, Entry.empty);

        if (self.indices.len > 0) {
            self.allocator.free(self.indices);
        }

        self.indices = new_indices;

        for (self.buckets.items, 0..) |*entry, i| {
            //TODO: should avoid these by adding free field to Entry.
            var in_free_list = false;
            var free_idx = self.free_list;
            while (free_idx != Entry.empty) {
                if (free_idx == i) {
                    in_free_list = true;
                    break;
                }
                free_idx = self.buckets.items[free_idx].next;
            }

            if (in_free_list) continue;
            const idx = try self.hashIndex(vm, entry.key);
            entry.next = self.indices[idx];
            entry.prev = Entry.empty;

            if (self.indices[idx] != Entry.empty) {
                self.buckets.items[self.indices[idx]].prev = @intCast(i);
            }

            self.indices[idx] = @intCast(i);
        }
        self.capacity = new_capacity;

        try self.buckets.ensureUnusedCapacity(self.allocator, self.capacity - self.buckets.items.len);
    }
};
