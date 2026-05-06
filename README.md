# Oak

Oak is a small scripting language. It's dynamically typed, compiles to bytecode, and runs on a stack-based VM. Everything is written in C17, with no real external dependencies beyond a tiny JSON library pulled in via CMake's FetchContent.

There's also a browser playground that runs the same VM compiled to WebAssembly — handy for poking at the language without setting up a toolchain.

---

## Building

You'll need CMake 3.20+ and any C17 compiler (MSVC 2022, GCC, and Clang all work).

```sh
cmake -S . -B build
cmake --build build
```

That gives you the `oak` executable. Run a script with:

```sh
oak path/to/script.oak [script args...]
```

A few flags are supported:

- `--disassemble` — print the bytecode chunk before running it
- `--no-debug` — skip debug logging
- `--help` — usage

## Tests

```sh
ctest --test-dir build -C Debug
```

Two flavors run side by side: small C harnesses (one per `.c` file under `tests/`) and golden script tests that execute every `.oak` file under `tests/scripts/` and diff its stdout against a `.expected` file. Scripts ending in `_main.oak` inside subdirectories are entry points for multi-file tests (see the import examples). A `.expected_error` file flips a test into "must fail with this stderr" mode.

## Web playground

The `www/` directory has a Vite-based playground that loads the WASM build. To play with it:

```sh
emcmake cmake -S . -B build_wasm
cmake --build build_wasm
npm install
npm run dev
```

---

## The language

### Bindings and assignment

```oak
let x = 42;          // immutable
let mut y = 10;      // mutable
y = 20;
y += 5;              // +=  -=  *=  /=  %=  also work
```

### Types

| Type     | Examples                                    |
|----------|---------------------------------------------|
| number   | `42`, `3.14`, `1e-3`                        |
| string   | `'hello'` (single-quoted only)              |
| bool     | `true`, `false`                             |
| array    | `[1, 2, 3]`, `[] as number[]`               |
| map      | `['a': 1, 'b': 2]`, `[:] as [string:number]`|
| record   | `new Point { x: 1, y: 2 }`                  |
| enum     | `Color.Red`                                 |

### Operators

```oak
x + y    x - y    x * y    x / y    x % y
x == y   x != y   x < y    x <= y   x > y    x >= y
a && b   a || b   !a       -x
```

`&&` and `||` short-circuit.

### Control flow

```oak
if x > 0 { print(x); } else { print(0); }

while x > 0 { x -= 1; }

for i from 0 to 10 { print(i); }   // half-open: [0, 10)

for v in arr { print(v); }
for i, v in arr { print(i); print(v); }

for k in map { print(k); }
for k, v in map { print(k); print(v); }

break;
continue;
```

### Functions

Functions can live at module level, or as methods inside a record. Both recursive and mutually recursive calls work.

```oak
fn add(a : number, b : number) -> number {
  return a + b;
}

print(add(1, 2));  // 3
```

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
print(p.dist_sq(new Point { x: 0, y: 0 }));

let mut q = new Point { x: 1, y: 1 };
q.translate(2, 3);
```

`self` is read-only; `mut self` lets the method mutate its receiver. Records can also be nested inside other records.

### Enums

```oak
enum Color { Red, Green, Blue }

let c = Color.Green;  // 1
```

Variants are just named integers.

### Arrays and maps

```oak
let mut nums = [] as number[];
nums.push(10);
nums.push(20);
print(nums.size());   // 2
print(nums[0]);       // 10

let mut m = [:] as [string:number];
m['x'] = 1;
print(m.has('x'));    // true
m.delete('x');

let scores = ['alice': 95, 'bob': 87];
```

### Strings

```oak
let s = 'hello' + ' ' + 'world';
print(s.size());

print('{0}+{1}={2}'.format([2, 3, 5]));   // 2+3=5
print('{}{}'.format(['a', 'b']));         // ab
```

### Modules and imports

A program can pull in other `.oak` files relative to its own directory:

```oak
import util.math;
import palette.color as col;

let x = math.double(21);
let c  = col.Color.Green;
```

Module names use dots for path separators; `as` gives an alias. Cycles are detected and reported as errors.

### Built-ins and the standard library

`print(v)` is built into the VM. Everything else is registered by the `oak` driver as native bindings:

| Method           | On         | What it does                          |
|------------------|------------|---------------------------------------|
| `.size()`        | array, map, string | length                        |
| `.push(v)`       | array      | append, returns new size              |
| `.has(k)`        | map        | does the key exist                    |
| `.delete(k)`     | map        | remove key, returns whether it existed |
| `.format(args)`  | string     | `{}` / `{n}` substitution from an array |

There's also a small `File` type:

```oak
let f = File.open('notes.txt', 'r');
let text = f.read_all();
f.close();
print(text);
```

`File` supports `open` (static), and `read`, `read_all`, `write`, `eof`, `close` on instances.

---

## Architecture

```
Source text
   │  oak_lexer       Lexer    → tokens
   │  oak_parser      Parser   → AST
   │  oak_compiler    Compiler → bytecode chunk
   │  oak_vm          Stack-based VM
   ▼
Result / runtime error
```

The same library powers the CLI and the WASM build; only the entry point differs.
