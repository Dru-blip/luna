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

        negate,
        not,
        un_plus,

        @"and",
        @"or",
        xor,

        mov, // uses bin
        branch, // uses tri
        jmp, // uses un

        store_global_by_index, // uses bin
        load_global_by_index, //uses bin
        store_global_by_name, //uses bin
        load_global_by_name, // uses bin

        build_function, // uses bin
        build_trace_and_throw_exception, //uses un
        raise_exception, // uses un

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

        call, // uses call
        super_call, //uses super_call,
        ret, // uses un
        ret_none, // uses un
        hlt, //uses none
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
            arg_offset: u32,
            argc: u32,
        },
        //INFO: i dont think i need super_call member, we can just reuse the call member.
        super_call: struct {
            method: u32,
            ret: u32,
            arg_offset: u32,
            argc: u32,
        },
    };
};

pub const Instructions = std.ArrayList(Inst);
pub const Constants = std.ArrayList(Value);

pub const RescueHandler = struct {
    exception_type: u32,
    handler_offset: u32,
    exception_register: ?u32 = null,
    exeception_class_loc: u32 = 0,
    ensure_offset: ?u32 = null,
};

pub const ExceptionHandlerBlock = struct {
    start_offset: u32,
    end_offset: u32,
    rescues: []RescueHandler,
    else_offset: ?u32 = null,
    ensure_offset: ?u32 = null,
};

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
    extra: []u32,
    exception_handlers: []ExceptionHandlerBlock,

    pub fn new(gc: *Gc) !*Executable {
        const obj = try gc.alloc(Executable);
        obj.class = gc.interpreter.base_class;
        return obj.as(Executable);
    }

    pub fn findExceptionHandlerBlockForOffset(executable: *Executable, ip: u32) ?ExceptionHandlerBlock {
        for (executable.exception_handlers) |handler| {
            if (handler.start_offset <= ip and ip < handler.end_offset) {
                return handler;
            }
        }
        return null;
    }

    pub const gc_hooks = Object.GcHooks{
        .name = "Executable",
        .visit = visit,
        .finalize = finalize,
    };

    fn finalize(self: *Object, gc: *Gc) void {
        const executable: *Executable = self.as(Executable);
        gc.gpa.free(executable.constants);
        gc.gpa.free(executable.spans);
        gc.gpa.free(executable.instructions);
        gc.gpa.free(executable.identifiers);
        gc.gpa.free(executable.extra);
        gc.gpa.free(executable.exception_handlers);
    }

    fn visit(self: *Object, live_objects: *ObjectSet) !void {
        try Object.Base.visit(self, live_objects);
        const executable: *Executable = self.as(Executable);
        for (executable.constants) |constant| {
            if (constant.asObject()) |obj| {
                try obj.gc_hooks.visit(obj, live_objects);
            }
        }
    }

    pub fn print(self: *const Executable) !void {
        var buf: [2048]u8 = undefined;
        var writer = std.fs.File.stdout().writer(&buf);
        const out = &writer.interface;

        for (self.constants) |constant| {
            if (constant.asObject()) |obj| {
                if (obj.gc_hooks == &Executable.gc_hooks) {
                    const child_executable: *Executable = obj.as(Executable);
                    try child_executable.print();
                }
            }
        }

        try out.print(
            "[generated bytecode for executable: '{s}']({s})\n",
            .{ self.name.asSlice(), self.filepath },
        );
        try out.print("Parameter count: {d}\n", .{self.param_count});
        try out.print("Register count: {d}\n", .{self.max_register_count});
        try out.print("Global count: {d}\n", .{self.global_variable_count});
        try out.print("Instructions : {d}\n", .{self.instructions.len});

        for (self.instructions, 0..) |inst, pc| {
            try out.print("\t@{d: >4} : ", .{pc});

            switch (inst.op) {
                .hlt => try out.print("Hlt", .{}),

                .load_const => try out.print(
                    "LoadConst r{d}, [{d}]",
                    .{ inst.data.bin.lhs, inst.data.bin.rhs },
                ),

                .load_true => try out.print("LoadTrue r{d}", .{inst.data.un}),

                .load_false => try out.print("LoadFalse r{d}", .{inst.data.un}),

                .load_none => try out.print("LoadNone r{d}", .{inst.data.un}),

                .load_undefined => try out.print("LoadUndefined r{d}", .{inst.data.un}),

                .load_global_by_name => try out.print(
                    "LoadGlobalByName r{d}, [{d}]",
                    .{ inst.data.bin.rhs, inst.data.bin.lhs },
                ),
                .store_global_by_name => try out.print(
                    "StoreGlobalByName [{d}], r{d}",
                    .{ inst.data.bin.rhs, inst.data.bin.lhs },
                ),

                .load_ident => try out.print(
                    "LoadIdent r{d}, [{d}]",
                    .{ inst.data.bin.rhs, inst.data.bin.lhs },
                ),
                .mov => try out.print(
                    "Mov r{d}, r{d}",
                    .{ inst.data.bin.lhs, inst.data.bin.rhs },
                ),
                .negate => {
                    try out.print("Neg r{d} -r{d}", .{ inst.data.bin.rhs, inst.data.bin.lhs });
                },
                .not => {
                    try out.print("Not r{d} -r{d}", .{ inst.data.bin.rhs, inst.data.bin.lhs });
                },
                .un_plus => {
                    try out.print("UnPlus r{d} +r{d}", .{ inst.data.bin.rhs, inst.data.bin.lhs });
                },
                .add => try out.print(
                    "Add r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .sub => try out.print(
                    "Sub r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .mul => try out.print(
                    "Mul r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .div => try out.print(
                    "Div r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .mod => try out.print(
                    "Mod r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .test_eq => try out.print(
                    "TestEq r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .test_neq => try out.print(
                    "TestNeq r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .test_lt => try out.print(
                    "TestLt r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .test_gt => try out.print(
                    "TestGt r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .test_le => try out.print(
                    "TestLe r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .test_ge => try out.print(
                    "TestGe r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),
                .@"and" => try out.print(
                    "And r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .@"or" => try out.print(
                    "Or r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),
                .xor => try out.print(
                    "Xor r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),
                .branch => try out.print(
                    "JumpIf r{d} ? {d} : {d}",
                    .{ inst.data.tri.op1, inst.data.tri.op2, inst.data.tri.dst },
                ),

                .jmp => try out.print("Jump {d}", .{inst.data.un}),

                .store_global_by_index => try out.print(
                    "StoreGlobalByIndex [{d}], r{d}",
                    .{ inst.data.bin.lhs, inst.data.bin.rhs },
                ),

                .load_global_by_index => try out.print(
                    "LoadGlobalByIndex r{d}, [{d}]",
                    .{ inst.data.bin.lhs, inst.data.bin.rhs },
                ),

                .build_function => try out.print(
                    "BuildFunction r{d}, entry={d}",
                    .{ inst.data.bin.lhs, inst.data.bin.rhs },
                ),

                .build_class => try out.print(
                    "BuildClass r{d}, [{d}]",
                    .{ inst.data.bin.rhs, inst.data.bin.lhs },
                ),

                .add_class_method => try out.print("AddClassMethod r{d}", .{inst.data.bin.rhs}),

                .set_super_class => try out.print(
                    "SetSuperClass r{d}, r{d}",
                    .{ inst.data.bin.lhs, inst.data.bin.rhs },
                ),

                .get_super_class => try out.print("GetSuperClass r{d}", .{inst.data.un}),

                .call => {
                    try out.print(
                        "Call r{d} <- r{d}(",
                        .{ inst.data.call.ret, inst.data.call.callee },
                    );
                    for (self.extra[inst.data.call.arg_offset .. inst.data.call.arg_offset + inst.data.call.argc], 0..) |arg, i| {
                        if (i != 0) try out.print(", ", .{});
                        try out.print("r{d}", .{arg});
                    }
                    try out.print(")", .{});
                },

                .super_call => try out.print("SuperCall", .{}),

                .build_dict => try out.print("BuildDict r{d}", .{inst.data.un}),

                .add_dict_entry => try out.print(
                    "AddDictEntry r{d}, r{d}, r{d}",
                    .{ inst.data.tri.dst, inst.data.tri.op1, inst.data.tri.op2 },
                ),

                .build_list => try out.print("BuildList r{d}", .{inst.data.un}),

                .append_list_item => try out.print("AppendListItem", .{}),

                .get_attribute => try out.print("GetAttribute", .{}),

                .set_attribute => try out.print("SetAttribute", .{}),

                .get_item => try out.print("GetItem", .{}),

                .set_item => try out.print("SetItem", .{}),

                .get_iter => try out.print("GetIter", .{}),

                .iter_next => try out.print("IterNext", .{}),

                .build_trace_and_throw_exception => try out.print("Throw", .{}),

                .raise_exception => try out.print("RaiseException r{d}", .{inst.data.un}),

                .ret => try out.print("Return r{d}", .{inst.data.un}),

                .ret_none => try out.print("ReturnNone", .{}),
            }
            try out.print("\n", .{});
        }
        try out.flush();
    }
};
