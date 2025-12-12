const std = @import("std");
const Value = @import("../core/Value.zig");
const PropertyMap = @import("../runtime/property_map.zig").PropertyMap;
const Object = @import("../runtime/Object.zig");

const Gc = @This();

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

    pub inline fn indexOf(blk: *Block, value: *Cell) usize {
        //does not check whether the value is within the block's range
        return (@intFromPtr(value) - @intFromPtr(blk.data.ptr)) / blk.cell_size;
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
) !*T {
    //TODO: should check alignment,
    // if requires should align
    const obj_size = @sizeOf(T);
    const header_size = @sizeOf(Object);
    const cell_size = header_size + obj_size;

    const block = gc.findSuitableBlock(cell_size) orelse try gc.createBlock(cell_size);

    const cell = block.allocateCell() orelse return std.mem.Allocator.Error.OutOfMemory;

    const obj_base = @intFromPtr(cell);
    const header: *Object = @ptrFromInt(obj_base);
    const obj_ptr: *T = @ptrFromInt(obj_base + header_size);

    if (!@hasDecl(T, "type_descriptor")) {
        @compileError("type_descriptor field must be present");
    }

    header.type_descriptor = &T.type_descriptor;
    header.property_map = PropertyMap.init(gc.gpa);
    header.ptr = obj_ptr;

    return @ptrCast(obj_ptr);
}

pub fn alloc(gc: *Gc, comptime T: anytype) !*T {
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

inline fn findSuitableBlock(gc: *Gc, cell_size: u32) ?*Block {
    for (gc.blocks.items) |blk| {
        if (blk.cell_size >= cell_size) {
            return blk;
        }
    }
    return null;
}

pub fn collect(gc: *Gc) void {
    for (gc.blocks.items) |blk| {
        for (0..blk.cell_count) |i| {
            const cell = blk.cell(i);
            const index = blk.indexOf(cell);
            if (blk.bitmap.isSet(index)) {
                const obj: *Object = @ptrCast(cell);
                obj.type_descriptor.finalize(obj.ptr, gc);
            }
        }
    }
}
