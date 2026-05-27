# Oak

Oak is a small dynamically typed scripting language. Source is parsed into
bytecode and executed by a stack-based VM written in C17. The CLI and the
browser playground use the same core library; the web version runs it through
WebAssembly.

## Build And Run

```sh
meson setup build
meson compile -C build
./build/oak path/to/script.oak [script args...]
```

Useful CLI flags:

| Flag | Purpose |
|---|---|
| `--disassemble` | print bytecode before running |
| `--no-debug` | compile without debug metadata |
| `--track-memory` | fail if tracked runtime allocations leak |
| `--help` | show usage |

Run tests with:

```sh
meson test -C build
```

The test suite includes C harnesses under `tests/` and smoke tests for the
top-level examples in `examples/`.

## Web Playground

```sh
meson setup build_wasm --cross-file meson/cross/emscripten.ini
meson compile -C build_wasm
npm install
npm run dev
```

## Language Snapshot

```oak
let x = 42;
let mut y = 10;
y += 5;

if y > x { print(y); } else { print(x); }
while y > 0 { y -= 1; }
for i from 0 to 10 { print(i); }
for value in [2, 3, 5] { print(value); }
for i, value in [2, 3, 5] { print(i + value); }
```

Core value types are `number`, `string`, `bool`, arrays, maps, records, enums,
functions, `none`, and weak references. Strings are single-quoted. Functions and
records support type parameters (`<T>`) for compile-time generics.

Operators include arithmetic (`+ - * / // %`), comparison, `!`, and short-circuit
logic (`&&`, `||`, `and`, `or`, `not`).

Functions live at module scope:

```oak
fn add(a : number, b : number) -> number {
  return a + b;
}
```

Records are data-first. Methods are declared separately:

```oak
record Point {
  x : number;
  y : number;
}

fn Point.dist_sq(self, other : Point) -> number {
  let dx = self.x - other.x;
  let dy = self.y - other.y;
  return dx * dx + dy * dy;
}

fn Point.move_by(mut self, dx : number, dy : number) {
  self.x += dx;
  self.y += dy;
}
```

Enums lower to integers:

```oak
enum Color { Red, Green, Blue }
print(Color.Green);  /* 1 */
```

Arrays and maps are mutable through methods and indexing:

```oak
let mut nums = [] as number[];
nums.push(10);
nums.push(20);
print(nums[0]);

let mut scores = [:] as [string:number];
scores['alice'] = 95;
print(scores.has('alice'));
for name, score in scores { print(name + ': {}'.format([score])); }
scores.delete('alice');
```

Traits describe required methods and support dynamic dispatch:

```oak
trait Shape {
  fn area(self) -> number;
}

record Rect { w : number; h : number; }
fn Rect.area(self) -> number { return self.w * self.h; }

let r = new Rect { w: 4, h: 5 };
let mut shapes = [] as Shape[];
shapes.push(r);
print(shapes[0].area());
```

`none` is a first-class empty value. Weak references use `Type weak`; expired
weak references compare like `none`.

```oak
record Node { next : Node weak; }
let empty = new Node { next: none };
print(empty.next == none);
```

Modules use selective or wildcard imports; all top-level declarations are public:

```oak
/* In util/math.oak */
fn add(a : number, b : number) -> number { return a + b; }

/* In main.oak */
import { add } from util.math;         /* selective */
import * from io;                      /* wildcard — all declarations */
import { add as sum } from util.math;  /* rename on import */
```

Generic functions and records use angle-bracket type parameters. The compiler
enforces type consistency through unification; at runtime, type parameters are
erased (all Oak values share the same representation, so a single bytecode body
serves every instantiation). Types are inferred from arguments at call sites:

```oak
fn identity<T>(x: T) -> T { return x; }
print(identity(42));
print(identity('hello'));

fn pick_first<A, B>(a: A, b: B) -> A { return a; }
print(pick_first(1, 'two'));

record Box<T> { value: T; }
let b = new Box { value: 42 };
print(b.value);

fn Box.get(self) -> T { return self.value; }
print(b.get());

fn unwrap<T>(b: Box<T>) -> T { return b.value; }
print(unwrap(b));

record Container<T> { items: T[]; }
let c = new Container { items: [1, 2, 3] };
print(c.items[0]);
```

Attributes attach metadata to declarations and are interpreted by embedding C
code:

```oak
@Deprecated
fn old_name() {}
```

## Standard Library

Built-in collection and string methods:

| Method | Receiver | Purpose |
|---|---|---|
| `.size()` | array, map, string | length |
| `.push(v)` | array | append and return new size |
| `.has(k)` | map | key lookup |
| `.delete(k)` | map | remove key |
| `.format(args)` | string | `{}` / `{n}` substitution |

Number helpers:

| Function | Purpose |
|---|---|
| `to_int(v)` / `to_float(v)` | numeric conversion |
| `is_int(v)` / `is_float(v)` | inspect numeric storage |
| `sqrt`, `sin`, `cos`, `tan` | basic math (`import * from math;`) |
| `abs`, `fmod`, `min`, `max`, `random` | more math helpers |

File I/O lives in `io`:

```oak
import * from io;

let f = File.open('notes.txt', FileMode.Read);
let text = f.read_all();
f.close();
print(text);
```

`File` also exposes `read()`, `write(value)`, `eof()`, and `close()`.

## Naming

Oak source uses:

| Thing | Style | Example |
|---|---|---|
| variables, functions, methods, fields | `snake_case` | `collect_primes`, `point_count` |
| records, traits, enums | `PascalCase` | `FileHandle`, `FileMode` |
| enum variants, attributes | `PascalCase` | `FileMode.Read`, `@Deprecated` |

## Native C Bindings

Native bindings are registered on `oak_compile_options_t` before
`oak_compile_ex()`. Use `oak_bind_type*` for native records,
`oak_bind_fn_global()` for global or module functions, `oak_bind_fn()` for
methods, `oak_bind_enum*` for native enums, and `oak_bind_attr()` for
attributes.

```c
struct oak_compile_options_t opts;
oak_compile_options_init(&opts, allocator);

struct oak_bind_type_t* file_type =
    oak_bind_type_in_module(&opts, "io", OAK_BIND_TYPE_RECORD, "File");

oak_bind_fn(&opts, &(struct oak_bind_fn_t){
    .kind = OAK_BIND_FN_INSTANCE_METHOD,
    .receiver_type_id = file_type->type_id,
    .name = "read_all",
    .impl = file_read_all,
    .arity = 0,
    .return_type_id = OAK_TYPE_STRING,
});

struct oak_compile_result_t result;
oak_compile_ex(ast_root, &opts, &result);
oak_compile_options_free(&opts);
```

Native functions use this shape:

```c
enum oak_fn_call_result_t fn(struct oak_native_ctx_t* ctx,
                             const struct oak_value_t* args,
                             int argc,
                             struct oak_value_t* out);
```

Return `OAK_FN_CALL_OK` on success or `OAK_FN_CALL_RUNTIME_ERROR` to raise a VM
runtime error. `OAK_TYPE_VOID` functions may leave `*out` untouched; the VM
treats the result as `none`.

Wrap native instances with `oak_native_record_new(ctx->allocator, type, ptr)`;
inside getters, setters, and methods, recover the C pointer with
`oak_native_instance(value)`. If `type->destructor` is set, it runs when the Oak
wrapper's final reference is released.

Native module types need a matching bodyless Oak stub, for example
`stdlib/io.oak`:

```oak
enum FileMode { Read, Write, Append }
record File;

fn File.open(path : string, mode : FileMode) -> File;
fn File.read(self) -> string;
fn File.read_all(self) -> string;
fn File.write(mut self, value : string);
fn File.eof(self) -> bool;
fn File.close(self);
```

## Architecture

```
source -> lexer -> parser -> compiler -> bytecode -> VM
```
