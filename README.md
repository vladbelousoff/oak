# Oak

Oak is a small, dynamically-typed scripting language compiled to bytecode and executed by a stack-based virtual machine.  It is written in C17 and has no external dependencies.

---

## Building

**Requirements:** CMake ≥ 3.20 and a C17-compatible compiler (MSVC 2022, GCC, or Clang).

```sh
cmake -S . -B build
cmake --build build
```

## Running the test suite

```sh
ctest --test-dir build -C Debug
```

`ctest` runs two kinds of tests:

- **C harnesses** under `tests/lexer/`, `tests/parser/`, `tests/compiler/`, `tests/vm/`, and `tests/stdlib/` — each `.c` file builds into its own small executable.
- **Script tests** that invoke `oak` on every `tests/scripts/*.oak` file (working directory = project root) and diff stdout against the matching `tests/scripts/*.expected` file via [`cmake/run_script_test.cmake`](cmake/run_script_test.cmake). A script test passes when `oak` exits with status 0 *and* the captured output matches the golden file.

A typical full run:

```
100% tests passed, 0 tests failed out of 109
```

---

## Language overview

### Variables

```oak
let x = 42;          // immutable binding
let mut y = 10;      // mutable binding
y = 20;
y += 5;              // +=  -=  *=  /=  %=  all supported
```

### Types

| Type | Literals |
|------|----------|
| `number` (int or float) | `42`, `3.14`, `1e-3` |
| `string` | `'hello'` (single-quoted; double quotes are not string delimiters) |
| `bool` | `true`, `false` |
| `array` | `[1, 2, 3]`, `[] as number[]` |
| `map` | `['a': 1, 'b': 2]`, `[:] as [string:number]` |
| record | `new Point { x: 1, y: 2 }` |

### Operators

```oak
// Arithmetic
x + y    x - y    x * y    x / y    x % y

// Comparison
x == y   x != y   x < y   x <= y   x > y   x >= y

// Logical (short-circuit)
a && b   a || b   !a

// Unary
-x
```

### Control flow

```oak
if x > 0 {
  print(x);
} else {
  print(0);
}

while x > 0 {
  x -= 1;
}

for i from 0 to 10 {   // i in [0, 10)
  print(i);
}

for v in arr {          // iterate array values
  print(v);
}

for i, v in arr {       // index + value
  print(i);
}

for k in map {          // iterate map keys
  print(k);
}

for k, v in map {       // key + value
  print(k);
  print(v);
}

break;
continue;
```

### Functions

```oak
fn add(a : number, b : number) -> number {
  return a + b;
}

print(add(1, 2));   // 3
```

User functions are declared as instance methods inside a `record TypeName { }` block.  Recursion and mutual recursion are supported.

### Records

```oak
record Point {
  x : number;
  y : number;
  fn dist_sq(self, other : Point) -> number {
    let dx = self.x - other.x;
    let dy = self.y - other.y;
    return dx * dx + dy * dy;
  }
  fn translate(mut self, dx : number, dy : number) {
    self.x = self.x + dx;
    self.y = self.y + dy;
  }
}

let p = new Point { x: 3, y: 4 };
print(p.x);
print(p.dist_sq(new Point { x: 0, y: 0 }));

let mut q = new Point { x: 1, y: 1 };
q.translate(2, 3);
```

### Enums

Enum variants are lowered to named integer constants and are accessed with the `EnumName.Variant` syntax.

```oak
enum Color { Red, Green, Blue }

let c = Color.Green;  // c == 1
```

### Arrays

```oak
let mut nums = [] as number[];
nums.push(10);
nums.push(20);
print(nums.size());  // 2
print(nums[0]);      // 10
```

### Maps

```oak
let mut m = [:] as [string:number];
m['x'] = 1;
print(m.size());    // 1
print(m.has('x'));  // true
m.delete('x');

// Literal form
let scores = ['alice': 95, 'bob': 87];
```

### Built-in functions

| Function | Description |
|----------|-------------|
| `print(v)` | Print a value followed by a newline |

### Collection methods

| Method | Receiver | Description |
|--------|----------|-------------|
| `.size()` | array, map | Number of elements |
| `.push(v)` | array | Append `v`; returns new size |
| `.has(k)` | map | `true` if key `k` exists |
| `.delete(k)` | map | Remove key `k`; returns `true` if it existed |

### Standard library (native bindings)

The `oak` driver registers a small set of **native** types and functions in addition to the table above. There is no `import` mechanism yet; these bindings are always registered when running a program through `oak`:

| Name | Role |
|------|------|
| File I/O (see `oak_stdlib_file.h`) | Path-based `read` / `write` helpers |

---

## Architecture

```
Source text
   │  oak_lexer       Lexer → token list
   │  oak_parser      Parser → AST
   │  oak_compiler    Compiler → bytecode chunk
   │  oak_vm          Stack-based VM
   ▼
Result / runtime error
```

Key limits:

- Stack depth: 256 values (`OAK_STACK_MAX`)
- Call frames: 64 (`OAK_FRAMES_MAX`)
- Constants per chunk: 65 536 (16-bit index via `OP_CONSTANT_LONG`)
- Jump offsets: 32-bit (≈ 4 GiB bytecode)
