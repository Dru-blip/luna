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
    return dict.map.get(vm, key);
}

pub fn remove(dict: *Dict, vm: *Vm, key: Value) ?Value {
    return dict.map.remove(vm, key);
}

pub fn contains(dict: *Dict, vm: *Vm, key: Value) !bool {
    return dict.map.contains(vm, key);
}

// pub fn clear(dict: *Dict) void {
//     dict.map.clearAndFree();
// }

pub fn size(dict: *Dict) usize {
    return dict.map.count;
}

fn visit(self: *Object, live_objects: *ObjectSet) !void {
    try Object.Base.visit(self, live_objects);
    //  const dict: *Dict = self.as(Dict);

    // var iterator = dict.map.iterator();
    // while (iterator.next()) |entry| {
    //     if (entry.key_ptr.asObject()) |key_obj| {
    //         if (!live_objects.contains(key_obj)) {
    //             try key_obj.gc_hooks.visit(key_obj, live_objects);
    //         }
    //     }
    //     if (entry.value_ptr.asObject()) |value_obj| {
    //         if (!live_objects.contains(value_obj)) {
    //             try value_obj.gc_hooks.visit(value_obj, live_objects);
    //         }
    //     }
    // }
}

fn finalize(self: *Object, _: *Gc) void {
    const dict: *Dict = self.as(Dict);
    dict.map.deinit();
    // Object.Base.finalize(self, gc);
}

const HashMap = struct {
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
        free: bool,

        const empty: u32 = std.math.maxInt(u32);
    };

    pub fn init(allocator: std.mem.Allocator) Self {
        return .{
            .allocator = allocator,
            .indices = &.{},
        };
    }

    pub fn deinit(self: *Self) void {
        self.allocator.free(self.indices);
        self.buckets.deinit(self.allocator);
    }

    fn hash(_: *const Self, vm: *Vm, key: Value) u64 {
        _ = vm;
        return switch (key.type) {
            .number => blk: {
                const bits = @as(u64, @bitCast(key.data.number));
                break :blk std.hash.Wyhash.hash(0, std.mem.asBytes(&bits));
            },
            .bool => @intFromBool(key.data.bool),
            .none => 0,
            .undefined => 1,
            .object => blk: {
                const obj = key.toObject();

                if (obj.asString()) |str| {
                    return str.hash;
                }
                // TODO: Call __hash__ method on object.
                const addr = @intFromPtr(obj);
                break :blk std.hash.Wyhash.hash(0, std.mem.asBytes(&addr));
            },
        };
    }

    inline fn hashIndex(self: *const Self, vm: *Vm, key: Value) u32 {
        const h = self.hash(vm, key);
        return @intCast(h & (self.indices.len - 1));
    }

    fn eql(_: *Self, a: Value, b: Value) bool {
        if (a.type != b.type) return false;
        return switch (a.type) {
            .number => a.data.number == b.data.number,
            .bool => a.data.bool == b.data.bool,
            .none, .undefined => true,
            .object => blk: {
                const ao = a.toObject();
                const bo = b.toObject();
                if (ao == bo) break :blk true;
                if (ao.asString()) |as| {
                    if (bo.asString()) |bs| {
                        break :blk as.eql(bs);
                    }
                    break :blk false;
                }
                // TODO: Call __eql__ method on object
                break :blk false;
            },
        };
    }

    pub fn get(self: *Self, vm: *Vm, key: Value) ?Value {
        const index = self.getIndex(vm, key) orelse return null;
        return self.buckets.items[index].value;
    }

    pub fn getPtr(self: *Self, vm: *Vm, key: Value) ?*Value {
        const entry_index = self.getIndex(vm, key) orelse return null;
        return &self.buckets.items[entry_index].value;
    }

    pub fn contains(self: *Self, vm: *Vm, key: Value) bool {
        _ = self.getIndex(vm, key) orelse return false;
        return true;
    }

    pub fn put(self: *Self, vm: *Vm, key: Value, value: Value) !void {
        try self.growIfNeeded(vm, 1);
        self.putAssumeCapacity(vm, key, value);
    }

    pub fn remove(self: *Self, vm: *Vm, key: Value) ?Value {
        const index = self.getIndex(vm, key) orelse return null;
        const entry = &self.buckets.items[index];

        if (self.free_list == Entry.empty) {
            self.free_list = index;
        } else {
            const next = self.free_list;
            self.free_list = index;
            self.buckets.items[index].next = next;
        }
        self.free_count += 1;
        return entry.value;
    }

    pub fn putAssumeCapacity(self: *Self, vm: *Vm, key: Value, value: Value) void {
        if (self.getIndex(vm, key)) |entry_index| {
            self.buckets.items[entry_index].value = value;
            return;
        }

        //TODO: recalculating the hash is a waste of time , just retrieve the hash generated from getIndex call.
        const index = self.hashIndex(vm, key);

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

    pub fn getIndex(self: *Self, vm: *Vm, key: Value) ?u32 {
        const index = self.hashIndex(vm, key);
        var entry_index = self.indices[index];
        while (entry_index != Entry.empty) {
            const entry = &self.buckets.items[entry_index];
            if (self.eql(entry.key, key)) {
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
            const idx = self.hashIndex(vm, entry.key);
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
