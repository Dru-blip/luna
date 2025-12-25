const std = @import("std");
const Value = @import("../core/Value.zig");

const Object = @import("../runtime/Object.zig");
const ObjectSet = @import("../runtime/ObjectSet.zig");
const Interpreter = @import("../runtime/Interpreter.zig");

const Gc = @This();

const AllocationStrategy = enum {
    best_fit,
    first_fit,
};

const Block = struct {
    const DefaultSize = 1024 * 16; //16 KB

    cell_size: u32,
    cell_count: u32,
    free_list: ?*Cell,
    data: []u8,
    bitmap: std.DynamicBitSet,

    pub const Cell = struct {
        next: ?*Cell,
    };

    pub inline fn cell(blk: *Block, i: usize) *Cell {
        return @ptrCast(@alignCast(blk.data[i * blk.cell_size ..]));
    }

    pub fn allocateCell(blk: *Block) ?*Cell {
        if (blk.free_list) |node| {
            blk.bitmap.set(blk.indexOf(node));
            blk.free_list = node.next;
            return node;
        }
        return null;
    }

    pub inline fn deallocateCell(blk: *Block, c: *Cell, cell_index: usize) void {
        blk.bitmap.unset(cell_index);
        c.next = blk.free_list;
        blk.free_list = c;
    }

    pub inline fn indexOf(blk: *Block, value: *Cell) usize {
        // assumes the cell is inside  block.
        return (@intFromPtr(value) - @intFromPtr(blk.data.ptr)) / blk.cell_size;
    }
};

arena: std.heap.ArenaAllocator,
blocks: std.ArrayList(*Block) = .empty,
gpa: std.mem.Allocator,
interpreter: *Interpreter,
bytes_allocated_since_last_gc: u64 = 0,

const gc_threshold = 4 * 1024 * 1024; // 4 MB

pub fn init(allocator: std.mem.Allocator, interpreter: *Interpreter) Gc {
    return Gc{
        .arena = std.heap.ArenaAllocator.init(allocator),
        .gpa = allocator,
        .interpreter = interpreter,
    };
}

pub fn deinit(gc: *Gc) void {
    for (gc.blocks.items) |block| {
        block.bitmap.deinit();
        gc.gpa.free(block.data);
    }

    gc.blocks.deinit(gc.gpa);
    gc.arena.deinit();
}

inline fn allocImpl(
    gc: *Gc,
    comptime T: anytype,
) !*Object {
    //TODO: check alignment
    const obj_size = @sizeOf(T);
    const header_size = @sizeOf(Object);
    const cell_size = header_size + obj_size;

    if (gc.bytes_allocated_since_last_gc + cell_size > gc_threshold) {
        try gc.collectGarbage();
        gc.bytes_allocated_since_last_gc = 0;
    }

    const block = gc.findSuitableBlock(.first_fit, cell_size) orelse try gc.createBlock(cell_size);

    const cell = block.allocateCell() orelse return std.mem.Allocator.Error.OutOfMemory;

    gc.bytes_allocated_since_last_gc += cell_size;

    const obj_base = @intFromPtr(cell);
    const header: *Object = @ptrFromInt(obj_base);
    const obj_ptr: *T = @ptrFromInt(obj_base + header_size);

    if (!@hasDecl(T, "type_descriptor")) {
        @compileError("type_descriptor  must be present");
    }

    header.type_descriptor = &T.type_descriptor;
    header.ptr = obj_ptr;

    return header;
}

pub fn alloc(gc: *Gc, comptime T: anytype) !*Object {
    return gc.allocImpl(T);
}

fn createBlock(gc: *Gc, cell_size: u32) !*Block {
    const cell_count = Block.DefaultSize / cell_size;

    var block = try gc.arena.allocator().create(Block);
    block.* = .{
        .cell_count = cell_count,
        .cell_size = cell_size,
        .free_list = null,
        .bitmap = try std.DynamicBitSet.initEmpty(gc.gpa, cell_count),
        .data = try gc.gpa.alloc(u8, Block.DefaultSize),
    };

    for (0..cell_count) |i| {
        const cell = block.cell(i);

        if (i == cell_count - 1) {
            cell.next = null;
        } else {
            cell.next = block.cell(i + 1);
        }
    }

    block.free_list = block.cell(0);

    try gc.blocks.append(gc.gpa, block);

    return block;
}

inline fn findSuitableBlock(gc: *Gc, comptime strategy: AllocationStrategy, cell_size: u32) ?*Block {
    switch (strategy) {
        .first_fit => {
            for (gc.blocks.items) |blk| {
                if (blk.cell_size >= cell_size) {
                    return blk;
                }
            }
            return null;
        },
        //TODO: implement best fit allocation.
        .best_fit => unreachable,
    }
}

pub fn collectGarbage(gc: *Gc) !void {
    var roots = ObjectSet.init(gc.gpa);
    var live_objects = ObjectSet.init(gc.gpa);

    try gc.collectRoots(&roots);
    try gc.collectLiveObjects(&roots, &live_objects);

    gc.clearMarkBits();
    gc.markLiveObjects(&live_objects);
    gc.sweepDeadObjects();
}

fn collectRoots(gc: *Gc, roots: *ObjectSet) !void {
    try roots.add(Object.from(gc.interpreter.builtins));
    for (gc.interpreter.vm.records.items) |*record| {
        for (record.registers) |val| {
            if (val.asObject()) |obj| {
                try roots.add(obj);
            }
        }
    }

    var string_iter = gc.interpreter.string_interner.iterator();
    while (string_iter.next()) |string| {
        try roots.add(Object.from(string.*));
    }
}

fn collectLiveObjects(_: *Gc, roots: *ObjectSet, live_objects: *ObjectSet) !void {
    var iter = roots.iterator();
    while (iter.next()) |s| {
        const obj = s.*;
        try obj.type_descriptor.visit(obj, live_objects);
    }
}

fn clearMarkBits(gc: *Gc) void {
    for (gc.blocks.items) |blk| {
        for (0..blk.cell_count) |i| {
            const cell = blk.cell(i);
            const index = blk.indexOf(cell);
            if (blk.bitmap.isSet(index)) {
                const obj: *Object = @ptrCast(cell);
                obj.marked = false;
            }
        }
    }
}

fn markLiveObjects(_: *Gc, live_objects: *ObjectSet) void {
    var iter = live_objects.iterator();
    while (iter.next()) |s| {
        const obj = s.*;
        obj.marked = true;
    }
}

fn sweepDeadObjects(gc: *Gc) void {
    for (gc.blocks.items) |blk| {
        for (0..blk.cell_count) |i| {
            const cell = blk.cell(i);
            const index = blk.indexOf(cell);
            const obj: *Object = @ptrCast(cell);
            if (blk.bitmap.isSet(index) and !obj.marked) {
                obj.type_descriptor.finalize(obj, gc);
                blk.deallocateCell(cell, index);
            }
        }
    }
}
