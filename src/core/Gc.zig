const std = @import("std");
const LuObject = @import("LuObject.zig");
const Value = @import("Value.zig");

const Gc = @This();

const Block = struct {
    const DefaultSize = 4 * 1024 * 1024; //4MB

    cell_size: u32,
    cell_count: u32,
    free_list: ?*Cell,
    data: []u8,

    pub const Cell = struct {
        next: ?*Cell,
    };

    pub fn cell(blk: *Block, i: usize) *Cell {
        return @ptrCast(@alignCast(blk.data[i * blk.cell_size ..]));
    }

    pub fn allocateCell(blk: *Block) ?*Cell {
        if (blk.free_list) |node| {
            blk.free_list = node.next;
            return node;
        }
        return null;
    }
};

pub const GcObject = struct {
    marked: bool = false,
    ptr: *anyopaque,
    // vtable: *const VTable,

    pub const VTable = struct {
        finalize: *const fn (*anyopaque) void,
        visit: *const fn (*anyopaque, *Gc) void,
    };

    pub inline fn as(obj: *GcObject, comptime T: anytype) *T {
        return @ptrCast(@alignCast(obj.ptr));
    }

    pub inline fn from(ptr: *anyopaque) GcObject {
        return .{
            .ptr = ptr,
        };
    }
};

arena: std.heap.ArenaAllocator,
blocks: std.ArrayList(*Block),
gpa: std.mem.Allocator,

pub fn init(allocator: std.mem.Allocator) Gc {
    return Gc{
        .arena = std.heap.ArenaAllocator.init(allocator),
        .gpa = allocator,
        .blocks = .empty,
    };
}

pub fn deinit(_: *Gc) void {}

pub fn alloc(gc: *Gc, comptime T: anytype) !*T {
    const cell_size = @sizeOf(T);

    const block = gc.findSuitableBlock(cell_size) orelse try gc.createBlock(cell_size);

    const cell = block.allocateCell() orelse return std.mem.Allocator.Error.OutOfMemory;

    return @ptrCast(cell);
}

fn createBlock(gc: *Gc, cell_size: u32) !*Block {
    const cell_count = Block.DefaultSize / cell_size;

    var block = try gc.arena.allocator().create(Block);
    block.* = .{
        .cell_count = cell_count,
        .cell_size = cell_size,
        .free_list = null,
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

inline fn findSuitableBlock(gc: *Gc, cell_size: u32) ?*Block {
    for (gc.blocks.items) |blk| {
        if (blk.cell_size >= cell_size) {
            return blk;
        }
    }
    return null;
}
