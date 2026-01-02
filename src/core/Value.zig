const std = @import("std");
const Object = @import("../runtime/Object.zig");
const Class = @import("../runtime/Class.zig");
const String = @import("../runtime/String.zig");
const Vm = @import("../runtime/Vm.zig");
const Function = @import("../runtime/Function.zig");

const Value = @This();

type: Type,
data: Data,

pub const Type = enum {
    number,
    bool,
    none,
    undefined,
    object,
};

pub const Data = union(Type) {
    number: f64,
    bool: bool,
    none: void,
    undefined: void,
    object: *Object,
};

pub inline fn number(value: f64) Value {
    return .{
        .type = .number,
        .data = .{
            .number = value,
        },
    };
}

pub inline fn @"bool"(value: bool) Value {
    return if (value) True else False;
}

pub const None: Value = .{
    .type = .none,
    .data = .{
        .none = {},
    },
};

pub const Undefined: Value = .{
    .type = .undefined,
    .data = .{
        .undefined = {},
    },
};

pub const True: Value = .{
    .type = .bool,
    .data = .{
        .bool = true,
    },
};

pub const False: Value = .{
    .type = .bool,
    .data = .{
        .bool = false,
    },
};

pub inline fn object(obj: *Object) Value {
    return .{
        .type = .object,
        .data = .{
            .object = obj,
        },
    };
}

pub inline fn isNumber(value: Value) bool {
    return value.type == .number;
}

pub inline fn isBool(value: Value) bool {
    return value.type == .bool;
}

pub inline fn isObject(value: Value) bool {
    return value.type == .object;
}

pub inline fn isNone(value: Value) bool {
    return value.type == .none;
}

pub inline fn isUndefined(value: Value) bool {
    return value.type == .undefined;
}

pub inline fn toNumber(value: Value) i64 {
    return value.data.number;
}

pub inline fn toBool(value: Value) bool {
    return value.data.bool;
}

pub inline fn toObject(value: Value) *Object {
    return value.data.object;
}

pub inline fn asObject(value: Value) ?*Object {
    return if (value.type == .object) value.data.object else null;
}

pub inline fn asClass(value: Value) ?*Class {
    return if (value.type == .object) value.toObject().asClass() else null;
}

pub inline fn asNumber(value: Value) f64 {
    return switch (value.type) {
        .number => value.data.number,
        .bool => @as(f64, @floatFromInt(@intFromBool(value.data.bool))),
        else => unreachable,
    };
}

pub inline fn isFalsy(v: Value) bool {
    return v.type == .none or v.type == .undefined or (v.type == .bool and v.data.bool == false);
}

pub inline fn isTruthy(v: Value) bool {
    return !isFalsy(v);
}

pub inline fn isNumeric(value: Value) bool {
    return value.type == .number or value.type == .bool;
}

pub inline fn isString(value: Value) bool {
    return value.type == .object and value.toObject().type_descriptor == &String.type_descriptor;
}

pub inline fn getTypeString(v: Value) []const u8 {
    return switch (v.type) {
        .number => "number",
        .bool => "bool",
        .undefined => "undefined",
        .object => {
            const class = v.toObject().class;
            return class.name.asSlice();
        },
        .none => "none",
    };
}

pub fn eql(a: Value, b: Value) bool {
    if (a.type != b.type) return false;
    return switch (a.type) {
        .number => a.data.number == b.data.number,
        .bool => a.data.bool == b.data.bool,
        .none, .undefined => true,
        .object => {
            const ao = a.toObject();
            const bo = b.toObject();
            if (ao == bo) return true;
            if (ao.asString()) |as| {
                if (bo.asString()) |bs| {
                    return as.eql(bs);
                }
                return false;
            }
            return false;
        },
    };
}

pub fn toString(value: Value) []const u8 {
    var buf: [56]u8 = undefined;
    return switch (value.type) {
        .number => std.fmt.bufPrint(&buf, "{d}", .{value.data.number}) catch "0",
        .bool => if (value.data.bool) "true" else "false",
        .undefined => "undefined",
        .none => "none",
        else => unreachable,
    };
}
