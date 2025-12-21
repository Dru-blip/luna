const std = @import("std");

const Object = @import("Object.zig");
const Gc = @import("../core/Gc.zig");
const Vm = @import("Vm.zig");
const Value = @import("../core/Value.zig");
const String = @import("String.zig");

const StringPrototype = @This();

pub fn new(gc: *Gc) !*Object {
    var obj = try gc.alloc(Object.Base);

    try obj.defineNativeFunction(gc, "toString", toString, 0, false);
    // try obj.defineNativeFunction(gc, "length", length, 0, false);
    // try obj.defineNativeFunction(gc, "equals", equals, 1, false);
    // try obj.defineNativeFunction(gc, "charAt", charAt, 1, false);
    // try obj.defineNativeFunction(gc, "concat", concat, 1, false);

    try obj.defineProperty(gc, "prototype", Value.object(gc.interpreter.object_prototype));
    return obj;
}

fn toString(_: *Vm, this_obj: *Object, _: []Value) !Value {
    return Value.object(this_obj);
}

// fn length(_: *Vm, this_obj: *Object, _: []Value) !Value {
//     const str = this_obj.as(String);
//     return Value.int(@intCast(str.length));
// }

// fn equals(_: *Vm, this_obj: *Object, args: []Value) !Value {
//     //TODO: check if args[0] is a string
//     const a = this_obj.as(String);

//     const b = args[0].asObject().as(String);
//     return Value.bool(String.eql(a, b));
// }

// fn charAt(vm: *Vm, this_obj: *Object, args: []Value) !Value {
//     const str = this_obj.as(String);
//     if (args.len == 0) return Value.None;

//     const index = args[0].toInt();
//     if (index < 0 or index >= str.length) return Value.None;

//     const slice = str.asSlice();
//     const ch = slice[@intCast(index)];

//     const buf = [_]u8{ch};
//     const new_str = try String.new(vm.gc, &buf);

//     return Value.object(new_str);
// }

// fn concat(vm: *Vm, this_obj: *Object, args: []Value) !Value {
//     const a = this_obj.as(String);
//     if (args.len == 0 or !args[0].isObjectOf(String)) {
//         return Value.object(this_obj);
//     }

//     const b = args[0].asObject().as(String);

//     const a_slice = a.asSlice();
//     const b_slice = b.asSlice();

//     var buf = try vm.gc.gpa.alloc(u8, a_slice.len + b_slice.len);
//     defer vm.gc.gpa.free(buf);

//     @memcpy(buf[0..a_slice.len], a_slice);
//     @memcpy(buf[a_slice.len..], b_slice);

//     const new_str = try String.new(vm.gc, buf);
//     return Value.object(new_str);
// }
