# Oak

Oak is a small scripting language. It's dynamically typed, compiles to bytecode, and runs on a stack-based VM. Everything is written in C17, with no real external dependencies beyond a tiny JSON library pulled in via Meson WrapDB.

There's also a browser playground that runs the same VM compiled to WebAssembly — handy for poking at the language without setting up a toolchain.

---

## Building

You'll need Meson, Ninja, and any C17 compiler (MSVC 2022, GCC, and Clang all work).

```sh
meson setup build
meson compile -C build
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
meson test -C build
```

Two flavors run side by side: focused C harnesses under `tests/` and example smoke tests that execute top-level examples under `examples/*/*.oak`. A `.expected_error` file marks an example as "must fail"; otherwise the example must exit successfully.

## Web playground

The `www/` directory has a Vite-based playground that loads the WASM build. To play with it:

```sh
meson setup build_wasm --cross-file meson/cross/emscripten.ini
meson compile -C build_wasm
npm install
npm run dev
```

---

## The language

### Bindings and assignment

```oak
let x = 42;          /* immutable */
let mut y = 10;      /* mutable */
y = 20;
y += 5;              /* +=  -=  *=  /=  %=  also work */
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
x + y    x - y    x * y    x / y    x // y   x % y
x == y   x != y   x < y    x <= y   x > y    x >= y
a && b   a || b   !a       -x
```

`/` always returns a float. `//` performs integer division after converting both operands to integers. `&&` and `||` short-circuit.

### Control flow

```oak
if x > 0 { print(x); } else { print(0); }

while x > 0 { x -= 1; }

for i from 0 to 10 { print(i); }   /* half-open: [0, 10) */

for v in arr { print(v); }
for i, v in arr { print(i); print(v); }

for k in map { print(k); }
for k, v in map { print(k); print(v); }

break;
continue;
```

### Functions

Functions live at module level. Both recursive and mutually recursive calls work.

```oak
fn add(a : number, b : number) -> number {
  return a + b;
}

print(add(1, 2));  /* 3 */
```

### Records

Records are plain data. Methods are declared separately with `fn TypeName.method(...)`.

Use `record Name;` (no braces) for records with no fields, and `record Name { ... }` for records that have fields:

```oak
record Point {
  x : number;
  y : number;
}

record Tag;   /* no fields — use semicolon form, not empty braces */

fn Point.distSq(self, other : Point) -> number {
  let dx = self.x - other.x;
  let dy = self.y - other.y;
  return dx * dx + dy * dy;
}

fn Point.translate(mut self, dx : number, dy : number) {
  self.x = self.x + dx;
  self.y = self.y + dy;
}

let p = new Point { x: 3, y: 4 };
print(p.distSq(new Point { x: 0, y: 0 }));

let mut q = new Point { x: 1, y: 1 };
q.translate(2, 3);
```

`self` is read-only; `mut self` lets the method mutate its receiver. Static methods (no `self`) are called as `TypeName.method(...)`. Records can also be nested inside other records.

### Enums

```oak
enum Color { Red, Green, Blue }

let c = Color.Green;  /* 1 */
```

Variants are just named integers.

### Arrays and maps

```oak
let mut nums = [] as number[];
nums.push(10);
nums.push(20);
print(nums.size());   /* 2 */
print(nums[0]);       /* 10 */

let mut m = [:] as [string:number];
m['x'] = 1;
print(m.has('x'));    /* true */
m.delete('x');

let scores = ['alice': 95, 'bob': 87];
```

### Strings

```oak
let s = 'hello' + ' ' + 'world';
print(s.size());

print('{0}+{1}={2}'.format([2, 3, 5]));   /* 2+3=5 */
print('{}{}'.format(['a', 'b']));         /* ab */
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

Number helpers. Import `math` before using the `math.*` functions.

| Function       | What it does                                      |
|----------------|---------------------------------------------------|
| `toInt(v)`     | convert a number to an integer                    |
| `toFloat(v)`   | convert a number to a float                       |
| `isInt(v)`     | is the value stored as an integer number          |
| `isFloat(v)`   | is the value stored as a floating-point number    |
| `math.sqrt(v)` | square root                                       |
| `math.sin(v)`  | sine, in radians                                  |
| `math.cos(v)`  | cosine, in radians                                |
| `math.tan(v)`  | tangent, in radians                               |

There's also a small `File` type:

```oak
import io;

let f = io.File.open('notes.txt', io.FileMode.Read);
let text = f.readAll();
f.close();
print(text);
```

`io.File` supports `open` as a static method and `read`, `readAll`, `write`, `eof`, and `close` on instances. `io.File.open` takes an `io.FileMode` enum value as its second argument: `io.FileMode.Read`, `io.FileMode.Write`, or `io.FileMode.Append`.

---

## Naming conventions

| Thing | Convention | Examples |
|---|---|---|
| Variables and bindings | camelCase | `fruitTotal`, `isPrime` |
| Functions and methods | camelCase | `fn collectPrimes(...)`, `fn Point.distSq(...)` |
| Record fields | camelCase | `pointCount`, `firstName` |
| Record types | PascalCase | `record Point`, `record FileHandle` |
| Enum types | PascalCase | `enum FileMode` |
| Enum variants | PascalCase | `FileMode.Read`, `Priority.High` |

Single-word names (`x`, `radius`, `done`) are lowercase by default and need no special treatment.

---

## Native C bindings

The embedding API (`include/oak_bind.h`) lets C code expose types, functions, and enums to Oak source without modifying the compiler or VM.

All registrations go into an `oak_compile_options_t` before calling `oak_compile_ex()`:

```c
struct oak_compile_options_t opts;
oak_compile_options_init(&opts);

/* ... register types, functions, enums ... */

struct oak_compile_result_t result;
oak_compile_ex(ast_root, &opts, &result);
oak_compile_options_free(&opts);
```

### Registering a type

`oak_bind_type()` creates a native record type visible to Oak as a regular record. `oak_bind_type_in_module()` scopes it under a module name so it appears as `module.Type` after an `import module;`.

```c
struct oak_bind_type_t* t =
    oak_bind_type_in_module(&opts, "io", OAK_BIND_TYPE_RECORD, "File");
```

Fields are registered with `oak_bind_field()`. Each field needs a getter (and an optional setter; `NULL` makes the field read-only):

```c
oak_bind_field(t, &(struct oak_bind_field_t){
    .name = "size",
    .field_type_id = OAK_TYPE_NUMBER,
    .getter = my_size_getter,
    .setter = NULL,   /* read-only */
});
```

Inside a getter or setter, `oak_native_instance()` retrieves the underlying C pointer:

```c
static oak_value_t my_size_getter(oak_value_t self) {
    MyType* p = oak_native_instance(self);
    return OAK_VALUE_F64((double)p->size);
}
```

Wrap a C pointer in an Oak value with `oak_native_record_new()`. When the Oak side releases its last reference, the optional `destructor` callback runs:

```c
t->destructor = my_type_free;   /* called with the raw C pointer */
*out = oak_native_record_new(t, my_ptr);
```

### Registering functions

`oak_bind_fn()` covers three cases:

| `kind`                      | Called from Oak as        | `receiver_type_id`   |
|-----------------------------|---------------------------|----------------------|
| `OAK_BIND_FN_GLOBAL`        | `fn(args)`                | `OAK_TYPE_VOID`      |
| `OAK_BIND_FN_STATIC_METHOD` | `Type.fn(args)`           | the type's `type_id` |
| `OAK_BIND_FN_INSTANCE_METHOD` | `obj.fn(args)`          | the type's `type_id` |

```c
/* Static method: File.open(path, mode) -> File */
oak_bind_fn(&opts, &(struct oak_bind_fn_t){
    .kind            = OAK_BIND_FN_STATIC_METHOD,
    .receiver_type_id = t->type_id,
    .name            = "open",
    .impl            = file_open,
    .arity           = 2,
    .return_type_id  = t->type_id,
});

/* Instance method: f.readAll() -> string */
oak_bind_fn(&opts, &(struct oak_bind_fn_t){
    .kind            = OAK_BIND_FN_INSTANCE_METHOD,
    .receiver_type_id = t->type_id,
    .name            = "readAll",
    .impl            = file_read_all,
    .arity           = 0,
    .return_type_id  = OAK_TYPE_STRING,
});
```

For `INSTANCE_METHOD`, `arity` excludes the implicit `self`; the compiler adds one automatically.

The native function signature is always:

```c
enum oak_fn_call_result_t my_fn(struct oak_native_ctx_t* ctx,
                                const struct oak_value_t* args,
                                int argc,
                                struct oak_value_t* out);
```

Return `OAK_FN_CALL_OK` and write the return value into `*out`, or return `OAK_FN_CALL_RUNTIME_ERROR` to raise a runtime error.

### Registering enums

`oak_bind_enum()` / `oak_bind_enum_in_module()` create an Oak enum backed by C integer constants:

```c
struct oak_bind_enum_t* mode =
    oak_bind_enum_in_module(&opts, "io", "FileMode");
oak_bind_enum_variant(mode, "Read",   0);
oak_bind_enum_variant(mode, "Write",  1);
oak_bind_enum_variant(mode, "Append", 2);
```

In Oak: `io.FileMode.Read`, `io.FileMode.Write`, etc.

### Oak-side stub file

When a native type lives in a module, provide a matching `.oak` stub file (e.g. `stdlib/io.oak`) that declares the type and method signatures without bodies. The loader validates that every bodyless declaration has a corresponding native binding:

```oak
record File;

fn File.open(path : string, mode : number) -> File;
fn File.readAll(self) -> string;
fn File.close(self);
```

The stub is what Oak `import` resolves; the C binding supplies the actual implementation.

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
