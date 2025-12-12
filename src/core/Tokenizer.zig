const std = @import("std");

pub const Token = struct {
    tag: Tag,
    loc: Loc,

    pub const Loc = struct {
        start: u32,
        end: u32,
        line: u32,
        col: u32,

        pub fn merge(self: Loc, other: *const Loc) Loc {
            return .{
                .start = self.start,
                .end = other.end,
                .line = self.line,
                .col = self.col,
            };
        }
    };

    pub const Tag = enum {
        invalid,
        plus,
        plus_equal,
        minus,
        minus_equal,
        asterisk,
        asterisk_equal,
        slash,
        slash_equal,
        modulus,
        modulus_equal,

        angle_bracket_left,
        angle_bracket_right,
        angle_bracket_left_equal,
        angle_bracket_right_equal,

        bang,

        equal,
        equal_equal,
        bang_equal,

        ampersand,
        ampersand_ampersand,

        pipe,
        pipe_pipe,

        l_paren,
        r_paren,
        l_brace,
        r_brace,

        int,
        identifier,
        keyword_return,
        keyword_true,
        keyword_false,
        keyword_none,
        keyword_let,
        keyword_if,
        keyword_else,
        keyword_loop,
        keyword_while,
        keyword_break,
        keyword_continue,

        eof,
    };

    pub const keywords = std.StaticStringMap(Tag).initComptime(.{
        .{ "return", .keyword_return },
        .{ "true", .keyword_true },
        .{ "false", .keyword_false },
        .{ "none", .keyword_none },
        .{ "let", .keyword_let },
        .{ "if", .keyword_if },
        .{ "else", .keyword_else },
        .{ "loop", .keyword_loop },
        .{ "break", .keyword_break },
        .{ "continue", .keyword_continue },
        .{ "while", .keyword_while },
    });

    pub fn getKeyword(bytes: []const u8) ?Tag {
        return keywords.get(bytes);
    }
};

buffer: [:0]const u8,
index: u32,
line: u32,
col: u32,

pub const Tokens = std.ArrayList(Token);
const Tokenizer = @This();

pub fn dump(self: *Tokenizer, token: *const Token) void {
    std.debug.print("{s} \"{s}\"\n", .{ @tagName(token.tag), self.buffer[token.loc.start..token.loc.end] });
}

pub fn init(buffer: [:0]const u8) Tokenizer {
    return .{
        .buffer = buffer,
        .index = if (std.mem.startsWith(u8, buffer, "\xEF\xBB\xBF")) 3 else 0,
        .line = 1,
        .col = 1,
    };
}

const State = enum {
    start,
    identifier,
    plus,
    minus,
    asterisk,
    slash,
    modulus,
    int,
    equal,
    less,
    greater,
    bang,
    ampersand,
    pipe,
    invalid,
};

fn advance(self: *Tokenizer) void {
    self.index += 1;
    self.col += 1;
}

pub fn next(self: *Tokenizer) Token {
    var result: Token = .{
        .tag = undefined,
        .loc = .{
            .start = self.index,
            .line = self.line,
            .col = self.col,
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
                            .line = self.line,
                            .col = self.col,
                            .start = self.index,
                            .end = self.index,
                        },
                    };
                } else {
                    continue :state .invalid;
                }
            },
            ' ', '\t', '\r' => {
                self.advance();
                result.loc.start = self.index;
                continue :state .start;
            },
            '\n' => {
                self.line += 1;
                self.col = 1;
                self.index += 1;
                continue :state .start;
            },
            '(' => {
                self.advance();
                result.tag = .l_paren;
            },
            ')' => {
                self.advance();
                result.tag = .r_paren;
            },
            '{' => {
                self.advance();
                result.tag = .l_brace;
            },
            '}' => {
                self.advance();
                result.tag = .r_brace;
            },
            '+' => continue :state .plus,
            '-' => continue :state .minus,
            '*' => continue :state .asterisk,
            '/' => continue :state .slash,
            '%' => continue :state .modulus,
            '<' => continue :state .less,
            '>' => continue :state .greater,
            '=' => continue :state .equal,
            '!' => continue :state .bang,
            '&' => continue :state .ampersand,
            '|' => continue :state .pipe,
            '0'...'9' => {
                result.loc.start = self.index;
                result.tag = .int;
                continue :state .int;
            },
            'a'...'z', 'A'...'Z', '_' => {
                result.loc.start = self.index;
                result.tag = .identifier;
                continue :state .identifier;
            },
            else => continue :state .invalid,
        },
        .plus => {
            self.advance();
            switch (self.buffer[self.index]) {
                '=' => {
                    result.tag = .plus_equal;
                    self.advance();
                },
                else => result.tag = .plus,
            }
        },
        .minus => {
            self.advance();
            switch (self.buffer[self.index]) {
                '=' => {
                    result.tag = .minus_equal;
                    self.advance();
                },
                else => result.tag = .minus,
            }
        },
        .asterisk => {
            self.advance();
            switch (self.buffer[self.index]) {
                '=' => {
                    result.tag = .asterisk_equal;
                    self.advance();
                },
                else => result.tag = .asterisk,
            }
        },
        .slash => {
            self.advance();
            switch (self.buffer[self.index]) {
                '=' => {
                    result.tag = .slash_equal;
                    self.advance();
                },
                else => result.tag = .slash,
            }
        },
        .modulus => {
            self.advance();
            switch (self.buffer[self.index]) {
                '=' => {
                    result.tag = .modulus_equal;
                    self.advance();
                },
                else => result.tag = .modulus,
            }
        },
        .less => {
            self.advance();
            switch (self.buffer[self.index]) {
                '=' => {
                    result.tag = .angle_bracket_left_equal;
                    self.advance();
                },
                else => result.tag = .angle_bracket_left,
            }
        },
        .greater => {
            self.advance();
            switch (self.buffer[self.index]) {
                '=' => {
                    result.tag = .angle_bracket_right_equal;
                    self.advance();
                },
                else => result.tag = .angle_bracket_right,
            }
        },
        .equal => {
            self.advance();
            switch (self.buffer[self.index]) {
                '=' => {
                    result.tag = .equal_equal;
                    self.advance();
                },
                else => result.tag = .equal,
            }
        },
        .bang => {
            self.advance();
            switch (self.buffer[self.index]) {
                '=' => {
                    result.tag = .bang_equal;
                    self.advance();
                },
                else => result.tag = .bang,
            }
        },
        .ampersand => {
            self.advance();
            switch (self.buffer[self.index]) {
                '&' => {
                    result.tag = .ampersand_ampersand;
                    self.advance();
                },
                else => result.tag = .ampersand,
            }
        },
        .pipe => {
            self.advance();
            switch (self.buffer[self.index]) {
                '|' => {
                    result.tag = .pipe_pipe;
                    self.advance();
                },
                else => result.tag = .pipe,
            }
        },
        .identifier => {
            self.advance();
            switch (self.buffer[self.index]) {
                'a'...'z', 'A'...'Z', '_' => continue :state .identifier,
                else => {
                    var ident = self.buffer[result.loc.start..self.index];
                    ident = std.mem.trim(u8, ident, " \t\r\n;");

                    if (Token.getKeyword(ident)) |tag| {
                        result.tag = tag;
                    }
                },
            }
        },
        .int => {
            self.advance();
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
    }
    return tokens;
}
