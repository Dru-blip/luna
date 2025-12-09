const std = @import("std");

const Ast = @import("../Ast.zig");
const Span = @import("../Tokenizer.zig").Token.Loc;
const Value = @import("../Value.zig");
const Gc = @import("../Gc.zig");
const Lir = @import("Lir.zig");

ir: Lir,
gpa: std.mem.Allocator,
ast: Ast,
current_block: u32,
