const std = @import("std");

pub const Token = struct {
    tag: Tag,
    loc: Loc,

    pub const Loc = struct {
        start: usize,
        end: usize,
    };

    pub const Tag = enum {
        invalid,
        plus,
        minus,
        asterisk,
        slash,
        l_paren,
        r_paren,
        l_brace,
        r_brace,

        int,
        identifier,
        keyword_return,

        eof,
    };

    pub const keywords = std.StaticStringMap(Tag).initComptime(.{
        .{ "return", .keyword_return },
    });

    pub fn getKeyword(bytes: []const u8) ?Tag {
        return keywords.get(bytes);
    }
};

buffer: [:0]const u8,
index: usize,

pub const Tokens = std.ArrayList(Token);
const Tokenizer = @This();

pub fn dump(self: *Tokenizer, token: *const Token) void {
    std.debug.print("{s} \"{s}\"\n", .{ @tagName(token.tag), self.buffer[token.loc.start..token.loc.end] });
}

pub fn init(buffer: [:0]const u8) Tokenizer {
    return .{
        .buffer = buffer,
        .index = if (std.mem.startsWith(u8, buffer, "\xEF\xBB\xBF")) 3 else 0,
    };
}

const State = enum {
    start,
    identifier,
    plus,
    minus,
    asterisk,
    slash,
    int,
    invalid,
};

pub fn next(self: *Tokenizer) Token {
    var result: Token = .{
        .tag = undefined,
        .loc = .{
            .start = self.index,
            .end = undefined,
        },
    };

    state: switch (State.start) {
        .start => switch (self.buffer[self.index]) {
            0 => {
                if (self.index == self.buffer.len) {
                    return .{
                        .tag = .eof,
                        .loc = .{
                            .start = self.index,
                            .end = self.index,
                        },
                    };
                } else {
                    continue :state .invalid;
                }
            },
            ' ', '\t', '\r' => {
                self.index += 1;
                continue :state .start;
            },
            '+' => continue :state .plus,
            '-' => continue :state .minus,
            '*' => continue :state .asterisk,
            '/' => continue :state .slash,
            '0'...'9' => {
                result.tag = .int;
                continue :state .int;
            },
            'a'...'z', 'A'...'Z', '_' => {
                result.tag = .identifier;
                continue :state .identifier;
            },
            else => continue :state .invalid,
        },
        .plus => {
            self.index += 1;
            switch (self.buffer[self.index]) {
                else => result.tag = .plus,
            }
        },
        .minus => {
            self.index += 1;
            switch (self.buffer[self.index]) {
                else => result.tag = .minus,
            }
        },
        .asterisk => {
            self.index += 1;
            switch (self.buffer[self.index]) {
                else => result.tag = .asterisk,
            }
        },
        .slash => {
            self.index += 1;
            switch (self.buffer[self.index]) {
                else => result.tag = .slash,
            }
        },
        .identifier => {
            self.index += 1;
            switch (self.buffer[self.index]) {
                'a'...'z', 'A'...'Z', '_' => continue :state .identifier,
                else => {
                    const ident = self.buffer[result.loc.start..self.index];
                    if (Token.getKeyword(ident)) |tag| {
                        result.tag = tag;
                    }
                },
            }
        },
        .int => {
            self.index += 1;
            switch (self.buffer[self.index]) {
                '0'...'9' => continue :state .int,
                else => {},
            }
        },
        .invalid => {
            result.tag = .invalid;
        },
    }

    result.loc.end = self.index;
    return result;
}

pub fn tokenize(source: [:0]const u8, gpa: std.mem.Allocator) !Tokens {
    var tokens: Tokens = .empty;
    var tokenizer = Tokenizer.init(source);
    while (true) {
        const token = tokenizer.next();
        try tokens.append(gpa, token);
        if (token.tag == .eof) break;
        // tokenizer.dump(&token);
    }
    return tokens;
}
