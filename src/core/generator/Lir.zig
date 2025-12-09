const Value = @import("../Value.zig");
const std = @import("std");

const Lir = @This();

nodes: std.ArrayList(Node),
blocks: std.ArrayList(BasicBlock),
gpa: std.mem.Allocator,

pub const Node = struct {
    id: u32,
    op: Op,
    inputs: std.ArrayList(u32),
    users: std.ArrayList(u32),
    value: Value,
    block_id: u32,

    pub const Op = enum {
        constant,
        add,
        sub,
        mul,
        div,
        mod,
        test_lt,
        test_gt,
        test_le,
        test_ge,
        test_neq,
        test_eq,
    };
};

pub const BasicBlock = struct {
    id: u32,
    nodes: std.ArrayList(u32),
    terminator: Terminator,
    predecessors: std.ArrayList(u32),
    successors: std.ArrayList(u32),

    pub const Terminator = union(enum) {
        none: void,
        ret: u32,
        jump: u32,
        branch: struct {
            cond: u32,
            true_block: u32,
            false_block: u32,
        },
    };
};

pub fn init(gpa: std.mem.Allocator) !Lir {
    return .{
        .nodes = .empty,
        .blocks = .empty,
        .gpa = gpa,
    };
}

pub fn deinit(self: *Lir) void {
    for (self.nodes.items) |*node| {
        node.inputs.deinit(self.gpa);
        node.users.deinit(self.gpa);
    }
    for (self.blocks.items) |*block| {
        block.nodes.deinit();
        block.predecessors.deinit();
        block.successors.deinit();
    }
    self.nodes.deinit();
    self.blocks.deinit();
}

pub fn createBlock(self: *Lir) !u32 {
    const id: u32 = @intCast(self.blocks.items.len);
    try self.blocks.append(self.gpa, .{
        .id = id,
        .nodes = .empty,
        .terminator = .none,
        .predecessors = .empty,
        .successors = .empty,
    });
    return id;
}

fn addUser(self: *Lir, node_id: u32, user_id: u32) !void {
    var node = &self.nodes.items[node_id];
    try node.users.append(self.gpa, user_id);
}

pub fn addNode(self: *Lir, op: Node.Op, inputs: []const u32, value: ?Value, block_id: u32) !u32 {
    const id: u32 = @intCast(self.nodes.items.len);

    var inputs_list: std.ArrayList(u32) = .empty;
    try inputs_list.appendSlice(self.gpa, inputs);

    const users_list: std.ArrayList(u32) = .empty;

    const node = Node{
        .id = id,
        .op = op,
        .inputs = inputs_list,
        .users = users_list,
        .value = value,
        .block_id = block_id,
    };

    try self.nodes.append(self.gpa, node);
    try self.blocks.items[block_id].nodes.append(self.gpa, id);

    for (inputs) |input_id| {
        try self.addUser(input_id, id);
    }

    return id;
}

fn addConstantNode(self: *Lir, value: Value, block_id: u32) !u32 {
    const id: u32 = @intCast(self.nodes.items.len);

    const node = Node{
        .id = id,
        .op = .constant,
        .inputs = .empty,
        .users = .empty,
        .value = value,
        .block_id = block_id,
    };

    try self.nodes.append(self.gpa, node);
    try self.blocks.items[block_id].nodes.append(self.gpa, id);

    return id;
}

pub fn setTerminator(self: *Lir, block_id: u32, terminator: BasicBlock.Terminator) !void {
    self.blocks.items[block_id].terminator = terminator;

    switch (terminator) {
        .jump => |target| {
            try self.blocks.items[block_id].successors.append(self.gpa, target);
            try self.blocks.items[target].predecessors.append(self.gpa, block_id);
        },
        .branch => |br| {
            try self.blocks.items[block_id].successors.append(self.gpa, br.true_block);
            try self.blocks.items[block_id].successors.append(self.gpa, br.false_block);
            try self.blocks.items[br.true_block].predecessors.append(self.gpa, block_id);
            try self.blocks.items[br.false_block].predecessors.append(self.gpa, block_id);
        },
        else => {},
    }
}
