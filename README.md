# Luna

Luna is a dynamically typed programming language implemented in Zig. It includes features such as classes with inheritance,exception handling, first-class functions, collections like lists and dictionaries, iterators, range expressions, a module system, and garbage collection. The runtime, compiler, and standard library are fully self-contained with no external dependencies.

## Prerequisites

- [Zig](https://ziglang.org/) 0.15.2
- A C compiler (the runtime library links libc)

## Build

```sh
zig build
```

This produces two artifacts in `zig-out/`:

- `luna` - the executable interpreter
- `lunarun` - the dynamic runtime library (linked by the executable)

## Run

```sh
zig build run -- examples/hello.luna
```

Or invoke the built binary directly:

```sh
./zig-out/bin/luna examples/hello.luna
```

On Windows the `lunarun.dll` must be on the system PATH or next to the executable.

## Language Overview

### Variables

```luna
let name = "Luna"
```

### Functions

```luna
fn fib(n) {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

print(fib(35))
```

Anonymous functions:

```luna
let add = fn(a, b) { return a + b }
```

### Classes and Inheritance

```luna
class Animal {
    fn __init__(name) {
        this.name = name
    }

    fn speak() {
        print(this.name + " speaks")
    }
}

class Dog : Animal {
    fn speak() {
        print(this.name + " barks")
    }
}
```

Use `super` to call parent methods.

### Collections

Lists:

```luna
let items = [1, 2, 3]
items.append(4)
print(items[0])
```

Dicts:

```luna
let d = {"key": "value"}
d["new"] = 42
```

### Iteration

```luna
foreach item in collection {
    print(item)
}
```

Range expressions:

```luna
foreach i in 1..100 {
    sum += i
}
```

Custom iterators implement `__iter__` (returns self) and `__next__` (returns next value or `undefined` to stop).

### Exception Handling

```luna
guard {
    raise AError("something went wrong")
}
rescue AError => e {
    print(e)
}
ensure {
    print("cleanup")
}
```

Custom exception classes inherit from `Exception`:

```luna
class AError : Exception {}
```

### Modules

```luna
let mod = import("range")
```

The `import` builtin loads a `.luna` file or a directory containing an `__init__.luna` file. The module's return value becomes the import result.

### Built-in Globals

| Name | Description |
|------|-------------|
| `print(value)` | Print to stdout |
| `input(prompt)` | Read a line from stdin |
| `import(path)` | Load and return a module |
| `hash(value)` | Compute hash of a value |
| `len(value)` | Length of a string, list, or dict |
| `List` | List class |
| `Dict` | Dict class |
| `Range` | Range class |
| `Exception` | Base exception class |
| `cli` | CLI module (`cli.args()`, `cli.getenv()`) |
| `true`, `false`, `none`, `undefined` | Literals |

### Control Flow

- `if` / `else`
- `while`
- `loop` (infinite loop)
- `for (init; test; update)`
- `foreach x in iterable`
- `break` / `continue`
- `return`

### Operators

Arithmetic: `+`, `-`, `*`, `/`, `%` with compound assignments (`+=`, `-=`, etc.)
Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
Logical: `and`, `or`, `not` (`!`)
Bitwise: `|`, `^`, `&`
Range: `..` (e.g. `1..10`)
Membership: `.method()`, `[index]`

## Architecture

The pipeline from source to execution:

```
Source (.luna) --> Tokenizer --> Parser --> AST --> Generator --> Bytecode --> VM --> Result
```

## Examples

The `examples/` directory contains sample programs:

- `hello.luna` - Range iteration and arithmetic
- `fib.luna` - Recursive Fibonacci
- `dict.luna` - Dict with custom hashable class
- `guard.luna` - Nested exception handling with guard/rescue/ensure
- `imports.luna` - Module import usage
- `super.luna` - Class inheritance and super calls
- `tic_tac_toe.luna` - Interactive tic-tac-toe game
- `cli.luna` - CLI argument and environment access
- `range/` - Custom range module with `__init__.luna`
