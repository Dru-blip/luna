const std = @import("std");
const Span = @import("Tokenizer.zig").Token.Loc;
const Value = @import("Value.zig");

const Object = @import("../runtime/Object.zig");
const ObjectSet = @import("../runtime/ObjectSet.zig");
const Gc = @import("Gc.zig");
const String = @import("../runtime/String.zig");

pub const Inst = struct {
    op: Op,
    data: Data,

    pub const Op = enum {
        hlt, //uses none
        load_const, // uses bin
        load_true, // uses un
        load_false, // uses un
        load_none, // uses un
        load_ident, // uses un
        load_undefined,
        add, // uses tri
        sub, // uses tri
        mul, // uses tri
        div, // uses tri
        mod, // uses tri

        test_lt, // uses tri
        test_gt, // uses tri
        test_le, // uses tri
        test_ge, // uses tri
        test_neq, // uses tri
        test_eq, // uses tri

        mov, // uses bin
        branch, // uses tri
        jmp, // uses un

        store_global_by_index, // uses bin
        load_global_by_index, //uses bin
        store_global_by_name, //uses bin
        load_global_by_name, // uses bin

        build_function, // uses bin
        build_trace_and_throw_exception, //uses un

        call, // uses call
        super_call, //uses super_call,

        build_dict,
        add_dict_entry,

        build_list,
        append_list_item,

        get_attribute,
        set_attribute,
        get_item,
        set_item,

        get_iter,
        iter_next,

        build_class,
        add_class_method,
        set_super_class,
        get_super_class,

        ret, // uses un
        ret_none, // uses un
    };

    pub const Data = union {
        none: void,
        un: u32,
        bin: struct {
            lhs: u32,
            rhs: u32,
        },
        tri: struct {
            op1: u32,
            op2: u32,
            dst: u32,
        },
        call: struct {
            callee: u32,
            this: u32,
            ret: u32,
            args: []u32,
        },
        //INFO: i dont think i need super_call member, we can just reuse the call member.
        // either way they share the same space.
        super_call: struct {
            method: u32,
            ret: u32,
            args: []u32,
        },
    };
};

pub const Instructions = std.ArrayList(Inst);
pub const Constants = std.ArrayList(Value);

pub const Executable = struct {
    instructions: []Inst,
    spans: []Span,
    identifiers: []Value,
    constants: Constants.Slice,
    max_register_count: u32,
    global_variable_count: u32,
    name: *String,
    param_count: u8,
    filepath: []const u8,

    pub fn new(gc: *Gc) !*Executable {
        const obj = try gc.alloc(Executable);
        obj.class = gc.interpreter.base_class;
        return obj.as(Executable);
    }

    pub const type_descriptor = Object.TypeDescriptor{
        .name = "Executable",
        .visit = visit,
        .finalize = finalize,
    };

    fn finalize(self: *Object, gc: *Gc) void {
        const executable: *Executable = self.as(Executable);
        gc.gpa.free(executable.constants);
        gc.gpa.free(executable.spans);
        gc.gpa.free(executable.instructions);
    }

    fn visit(self: *Object, live_objects: *ObjectSet) !void {
        try Object.Base.visit(self, live_objects);
        const executable: *Executable = self.as(Executable);
        for (executable.constants) |constant| {
            if (constant.isObject()) {
                const object = constant.toObject();
                try object.type_descriptor.visit(object, live_objects);
            }
        }
    }

    pub fn print(self: *const Executable) !void {
        var stdout_buffer: [1024]u8 = undefined;
        var stdout_writer = std.fs.File.stdout().writer(&stdout_buffer);
        var stdout = &stdout_writer.interface;

        for (self.constants) |constant| {
            if (constant.asObject()) |obj| {
                if (obj.type_descriptor == &Executable.type_descriptor) {
                    const child_executable: *Executable = obj.as(Executable);
                    try child_executable.print();
                }
            }
        }

        try stdout.print("Executable<{s}> ({s}):{{\n", .{ self.name.asSlice(), self.filepath });
        try stdout.print("  Max registers : {d}\n", .{self.max_register_count});

        try stdout.print("  Constants ({d})\n", .{self.constants.len});

        try stdout.print("  Instructions:\n", .{});
        for (self.instructions, 0..) |inst, idx| {
            try stdout.print("  {d:0>4}: ", .{idx});

            //TODO: print instruction details
            switch (inst.op) {
                .hlt => try stdout.print("Hlt", .{}),
                .load_const => try stdout.print("LoadConst r{d} [const {d}]", .{ inst.data.bin.lhs, inst.data.bin.rhs }),
                .load_true => try stdout.print("LoadTrue r{d}", .{inst.data.un}),
                .load_false => try stdout.print("LoadFalse r{d}", .{inst.data.un}),
                .load_none => try stdout.print("LoadNone r{d}", .{inst.data.un}),
                .load_undefined => try stdout.print("LoadUndefined r{d}", .{inst.data.un}),
                .load_ident => try stdout.print("LoadIdent r{d} [ident {d}]", .{ inst.data.bin.rhs, inst.data.bin.lhs }),
                .add => try stdout.print("Add r{d} <- r{d} , r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .sub => try stdout.print("Sub r{d} <- r{d} , r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .mul => try stdout.print("Mul r{d} <- r{d} , r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .div => try stdout.print("Div r{d} <- r{d} , r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .mod => try stdout.print("Mod r{d} <- r{d} , r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .test_lt => try stdout.print("TestLt r{d} <- r{d} cmp r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .test_gt => try stdout.print("TestGt r{d} <- r{d} cmp r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .test_le => try stdout.print("TestLe r{d} <- r{d} cmp r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .test_ge => try stdout.print("TestGe r{d} <- r{d} cmp r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .test_neq => try stdout.print("TestNeq r{d} <- r{d} cmp r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .test_eq => try stdout.print("TestEq r{d} <- r{d} cmp r{d}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .mov => try stdout.print("Mov r{d} <- r{d}", .{ inst.data.bin.lhs, inst.data.bin.rhs }),
                .branch => try stdout.print("JumpIf r{d} goto {d} else goto {d}", .{ inst.data.tri.op1, inst.data.tri.op2, inst.data.tri.dst }),
                .jmp => try stdout.print("Jump {d}", .{inst.data.un}),
                .store_global_by_index => try stdout.print("StoreGlobalByIndex global[{d}] r{d}", .{ inst.data.bin.lhs, inst.data.bin.rhs }),
                .load_global_by_index => try stdout.print("LoadGlobalByIndex r{d}, global[{d}]", .{ inst.data.bin.lhs, inst.data.bin.rhs }),
                .store_global_by_name => try stdout.print("StoreGlobalByName names[{d}] r{d}", .{ inst.data.bin.lhs, inst.data.bin.rhs }),
                .load_global_by_name => try stdout.print("LoadGlobalByName r{d}, names[{d}]", .{ inst.data.bin.lhs, inst.data.bin.rhs }),
                .build_function => try stdout.print("MakeFunction r{d} e[{d}] names[{d}]", .{ inst.data.bin.lhs, inst.data.bin.rhs, inst.data.bin.rhs }),
                .build_class => try stdout.print("BuildClass r{d} names[{d}]", .{ inst.data.bin.rhs, inst.data.bin.lhs }),
                .add_class_method => try stdout.print("AddClassMethod r{d}", .{inst.data.bin.rhs}),
                .set_super_class => try stdout.print("SetSuperClass r{d}, r{d}", .{ inst.data.bin.lhs, inst.data.bin.rhs }),
                .get_super_class => try stdout.print("GetSuperClass r{d}", .{inst.data.un}),
                .build_trace_and_throw_exception => try stdout.print("BuildTraceAndThrowException", .{}),
                .call => {
                    try stdout.print("Call r{d} <- r{d}(", .{ inst.data.call.ret, inst.data.call.callee });
                    for (inst.data.call.args, 0..) |arg, i| {
                        if (i > 0) try stdout.print(", ", .{});
                        try stdout.print("r{d}", .{arg});
                    }
                    try stdout.print(")", .{});
                },
                .super_call => {
                    try stdout.print("SuperCall", .{});
                },
                .build_dict => try stdout.print("BuildDict r{d}", .{inst.data.un}),
                .add_dict_entry => try stdout.print("SetDictEntry r{d}={{r{d}:r{d}}}", .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 }),
                .build_list => try stdout.print("BuildList r{d}", .{inst.data.un}),
                .append_list_item => try stdout.print("AppendListItem ", .{}),
                .get_attribute => try stdout.print("GetAttribute ", .{}),
                .set_attribute => try stdout.print("SetAttribute", .{}),
                .set_item => try stdout.print("SetItem", .{}),
                .get_item => try stdout.print("GetItem", .{}),
                .get_iter => try stdout.print("GetIter", .{}),
                .iter_next => try stdout.print("IterNext", .{}),
                .ret => try stdout.print("Ret r{d}", .{inst.data.un}),
                .ret_none => try stdout.print("RetNone", .{}),
            }
            try stdout.print("\n", .{});
        }
        try stdout.print("}}\n", .{});
        try stdout.flush();
    }
};
