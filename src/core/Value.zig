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

pub fn number(value: f64) Value {
    return .{
        .type = .number,
        .data = .{
            .number = value,
        },
    };
}

pub fn @"bool"(value: bool) Value {
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

pub fn object(obj: *Object) Value {
    return .{
        .type = .object,
        .data = .{
            .object = obj,
        },
    };
}

pub fn isNumber(value: Value) bool {
    return value.type == .number;
}

pub fn isBool(value: Value) bool {
    return value.type == .bool;
}

pub fn isObject(value: Value) bool {
    return value.type == .object;
}

pub fn isNone(value: Value) bool {
    return value.type == .none;
}

pub fn isUndefined(value: Value) bool {
    return value.type == .undefined;
}

pub fn toNumber(value: Value) i64 {
    return value.data.number;
}

pub fn toBool(value: Value) bool {
    return value.data.bool;
}

pub fn toObject(value: Value) *Object {
    return value.data.object;
}

pub fn asObject(value: Value) ?*Object {
    return if (value.type == .object) value.data.object else null;
}

pub fn asClass(value: Value) ?*Class {
    return if (value.type == .object) value.toObject().asClass() else null;
}

pub fn asNumber(value: Value) f64 {
    return switch (value.type) {
        .number => value.data.number,
        .bool => @as(f64, @floatFromInt(@intFromBool(value.data.bool))),
        else => unreachable,
    };
}

pub fn asInt(value: Value) i64 {
    return switch (value.type) {
        .number => @as(i64, @bitCast(@round(value.data.number))),
        .bool => @as(i64, @intFromBool(value.data.bool)),
        else => unreachable,
    };
}

pub fn isFalsy(v: Value) bool {
    return v.type == .none or v.type == .undefined or (v.type == .bool and v.data.bool == false);
}

pub fn isTruthy(v: Value) bool {
    return !isFalsy(v);
}

pub fn isNumeric(value: Value) bool {
    return value.type == .number or value.type == .bool;
}

pub fn isString(value: Value) bool {
    return value.type == .object and value.toObject().gc_hooks == &String.gc_hooks;
}

pub fn getTypeString(v: Value) []const u8 {
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

pub fn toString(value: Value, buffer: []u8) []const u8 {
    return switch (value.type) {
        .number => std.fmt.bufPrint(buffer, "{any}", .{value.data.number}) catch "0",
        .bool => if (value.data.bool) "true" else "false",
        .undefined => "undefined",
        .none => "none",
        else => unreachable,
    };
}
