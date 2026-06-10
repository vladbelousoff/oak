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
| `--debug` | run the script under the interactive debugger |
| `--disassemble` | print bytecode before running |
| `--no-debug-symbols` (alias `--no-debug`) | compile without debug metadata |
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
functions, `none`, and weak references. Strings are single-quoted (double
quotes are a lexer error) and support the `\n`, `\t`, `\r`, `\\`, `\'`, and
`\"` escapes.

Heap values use reference counting with cycle collection. Unreachable ownership
cycles are reclaimed automatically; weak references remain useful for modelling
non-owning relationships explicitly.

Operators include arithmetic (`+ - * / // %`), comparison, `!`, and short-circuit
logic (`&&`, `||`, `and`, `or`, `not`).

Numbers are 32-bit integers or floats. `/` always produces a float; `//` is
integer division and `%` is integer remainder. Integer `+ - *` wrap on
overflow (two's complement); division by zero, `//` overflow, and `//`
operands outside the integer range raise runtime errors. Integer literals
outside the 32-bit range are rejected at lex time.

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
let mut nums = new number[];
nums.push(10);
nums.push(20);
print(nums[0]);

let mut scores = new [string:number];
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
let mut shapes = new Shape[];
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
    .receiver_type = file_type,
    .name = "read_all",
    .impl = file_read_all,
    .arity = 0,
    .return_type = OAK_BIND_SCALAR(OAK_TYPE_STRING),
});

struct oak_compile_result_t result;
oak_compile_ex(ast_root, &opts, &result);
oak_compile_options_free(&opts);
```

Field, parameter, and return types are described by `oak_bind_type_ref_t`,
built with the `OAK_BIND_*` helpers: `OAK_BIND_SCALAR(id)`,
`OAK_BIND_ARRAY(elem)`, and `OAK_BIND_MAP(key, value)` for builtins, or
`OAK_BIND_NATIVE(type)`, `OAK_BIND_NATIVE_ARRAY(type)`, and
`OAK_BIND_NATIVE_MAP(key_type, value_type)` for native type descriptors.

Native functions and methods may optionally describe their parameter types with
the `param_types` / `param_count` fields so the compiler can type-check call
sites. `param_types` is borrowed by the compiler and must outlive
`oak_compile_ex()` (use arrays with static or function-body lifetime, not
temporaries that go out of scope before compilation).

### Inline value types

`oak_bind_type(&opts, OAK_BIND_TYPE_VALUE, name)` registers a non-refcountable
*value type*: its payload (an opaque pointer or handle) lives directly inside
the 16-byte `oak_value_t` with no heap wrapper, no reference counting, and no
destructor. Copies are bitwise. Because inline values carry no runtime type
identity, value types expose data through **methods only** — `oak_bind_field`
rejects them.

```c
struct oak_bind_type_t* handle =
    oak_bind_type(&opts, OAK_BIND_TYPE_VALUE, "Handle");
oak_bind_fn(&opts, &(struct oak_bind_fn_t){
    .kind = OAK_BIND_FN_INSTANCE_METHOD,
    .receiver_type = handle,
    .name = "id", .impl = handle_id, .arity = 0,
    .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
});
```

Build a value with `oak_native_value_new(payload)` and recover the payload
inside methods with `oak_native_value(args[0])` (the analogues of
`oak_native_record_new` / `oak_native_instance` for heap records). Two inline
values compare equal when their payloads are identical.

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

Function and field bindings accept an optional `user_data` pointer. For
functions it is surfaced to the implementation as
`oak_native_ctx_t::user_data`; field getters and setters receive their
binding's pointer as a trailing `user_data` argument.

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

## C++ API

`include/oak.hpp` wraps the C API in a header-only C++20 layer for native
builds (the WASM build stays C-only). Every type is non-copyable and owns its
resources; move semantics apply where transfer makes sense. `raw()` accessors
drop down to the underlying C structs when needed.

```cpp
#include <oak.hpp>

oak::Allocator alloc;
oak::CompileOptions opts(alloc);
opts.with_stdlib();

auto result = oak::compile("print('hello');", opts);
if (result.ok())
{
  oak::VM vm(alloc);
  vm.run(result);
}
else
{
  for (int i = 0; i < result.error_count(); ++i)
  {
    auto d = result.error(i);
    fprintf(stderr, "%d:%d: %s\n", d.line(), d.column(), d.message());
  }
}
```

### `oak::Allocator`

RAII wrapper around `oak_allocator_t`. Must outlive every object that was
constructed with it.

```cpp
oak::Allocator alloc;                          // system allocator (default)
oak::Allocator tracking(oak::Allocator::tracking); // leak-detecting allocator
```

| Member | Description |
|---|---|
| `Allocator(kind k = system)` | `system` or `tracking` |
| `get()` | `oak_allocator_t*` |

### `oak::Value`

RAII wrapper around `oak_value_t` with automatic reference counting. Copy
increments the refcount; move steals without touching it.

**Static factories**

| Factory | Description |
|---|---|
| `Value::i32(n)` | 32-bit integer |
| `Value::f32(f)` | 32-bit float |
| `Value::boolean(b)` | bool |
| `Value::none()` | `none` |
| `Value::handle(h)` | opaque `uint64_t` handle |
| `Value::string(alloc, s)` | heap-allocated Oak string |

Implicit constructors exist for `int`, `float`, `double`, and `bool`.
Pointer-to-Value conversion is deleted to prevent accidental bool coercion.

**Type predicates**

`is_i32()`, `is_f32()`, `is_bool()`, `is_none()`, `is_number()`,
`is_string()`, `is_array()`, `is_map()`, `is_fn()`, `is_record()`,
`is_native_record()`, `is_handle()`, `is_truthy()`

**Extractors**

| Method | Returns |
|---|---|
| `as_i32()` | `int32_t` — coerces f32 if needed |
| `as_f32()` | `float` — coerces i32 if needed |
| `as_bool()` | `bool` |
| `as_handle()` | `uint64_t` |
| `as_string()` | `std::string_view` into the Oak heap |
| `as_native_instance()` | `void*` C pointer inside a native record |

**Other**

| Method | Description |
|---|---|
| `raw()` | `oak_value_t` (refcount unaffected) |
| `release()` | move out the raw value, leaving `none` here |
| `operator==` | structural equality |

### `oak::Args`

Non-owning view over the argument array passed to a native function. Returned
values are ref-counted copies.

| Member | Description |
|---|---|
| `size()` | argument count |
| `operator[](i)` | `Value` copy of argument `i` |
| `raw(i)` | `oak_value_t` without refcount bump |

### `oak::Context`

Thin wrapper around `oak_native_ctx_t*` passed into native function closures.

| Member | Description |
|---|---|
| `vm()` | `oak_vm_t*` |
| `allocator()` | `oak_allocator_t*` |
| `user_data()` | `void*` set via `VM::set_user_data()` |
| `raw()` | `oak_native_ctx_t*` |

### `oak::Diagnostic`

Read-only view into a compile or load error.

| Member | Description |
|---|---|
| `line()` | 1-based line number |
| `column()` | 1-based column |
| `message()` | null-terminated error string |

### `oak::CompileResult`

Move-only; owns the compiled `oak_chunk_t` and any diagnostics.

| Member | Description |
|---|---|
| `ok()` | true if compilation succeeded |
| `chunk()` | `oak_chunk_t*` |
| `error_count()` | number of diagnostics |
| `error(i)` | `Diagnostic` for error `i` |
| `raw()` | `oak_compile_result_t*` |

### `oak::CompileOptions`

Builder for compile-time configuration and native bindings. All `bind_*`
methods return `*this` for chaining. The object must outlive every chunk/VM
compiled with it.

**Configuration**

| Method | Description |
|---|---|
| `source_name(name)` | label used in diagnostics |
| `debug_info(bool)` | emit debug metadata (default on) |
| `with_stdlib()` | register the standard library |

**Global functions**

```cpp
// C++ closure — any callable (Context&, Args) -> Value
opts.bind_fn("print_twice", 1,
    [](oak::Context&, oak::Args args) -> oak::Value {
        auto s = args[0].as_string();
        printf("%.*s %.*s\n", (int)s.size(), s.data(),
                              (int)s.size(), s.data());
        return {};
    });

// In a named module
opts.bind_fn("mymod", "helper", 0, ...);

// Raw C callback
opts.bind_fn_raw("low_level", 0, my_c_fn,
                  OAK_BIND_SCALAR(OAK_TYPE_NUMBER));
```

Both `bind_fn` and `bind_fn_raw` accept an optional `return_type`
(`oak_bind_type_ref_t`, default `OAK_TYPE_VOID`) and, for the raw overloads,
an optional `user_data` pointer.

**Native types**

Returns a `TypeBuilder<T>` for fluent method and field registration:

```cpp
auto tb = opts.bind_type<MyObj>("MyObj");                   // global
auto tb = opts.bind_type<MyObj>("mymod", "MyObj");          // in module
auto tb = opts.bind_type<MyObj>("Handle", OAK_BIND_TYPE_VALUE); // value type
```

**Enums**

```cpp
opts.bind_enum("Color", {{"Red", 0}, {"Green", 1}, {"Blue", 2}});
opts.bind_enum("io", "FileMode", {{"Read", 0}, {"Write", 1}});
```

**Attributes**

```cpp
opts.bind_attr(&my_attr_desc);  // oak_bind_attr_t* from the C API
```

| Method | Description |
|---|---|
| `raw()` | `oak_compile_options_t*` |

### `oak::TypeBuilder<T>`

Returned by `CompileOptions::bind_type<T>()`. All methods return `*this`.

**Fields** (heap record types only — rejected for value types)

```cpp
// Member pointer — auto getter/setter for int, float, bool members
tb.field("x", &MyObj::x);

// Explicit getter/setter with a type descriptor
tb.field("label", OAK_BIND_SCALAR(OAK_TYPE_STRING), my_getter, my_setter);
```

**Methods**

```cpp
// Instance method: args[0] is `self`
tb.method("area", 0,
    [](oak::Context&, oak::Args args) -> oak::Value {
        auto* obj = static_cast<MyObj*>(args[0].as_native_instance());
        return oak::Value(obj->width * obj->height);
    }, OAK_BIND_SCALAR(OAK_TYPE_NUMBER));

// Static method: no implicit self
tb.static_method("create", 0, ...);
```

**Destructor** (heap record types)

```cpp
tb.destructor();              // delete-based; use only for `new T` instances
tb.destructor([](void* p) {   // custom; must free the pointer itself
    free_my_obj(static_cast<MyObj*>(p));
});
```

| Method | Description |
|---|---|
| `raw()` | `oak_bind_type_t*` |

### `oak::VM`

Stack-based bytecode interpreter. Non-copyable, non-movable.

```cpp
oak::VM vm(alloc);
vm.set_user_data(&app_state);

// Run a chunk produced by compile()
vm.run(result);

// Call an Oak function value retrieved from the chunk
oak::Value out;
vm.call(fn_val, {oak::Value(1), oak::Value(2)}, &out);
```

| Method | Description |
|---|---|
| `run(chunk)` / `run(cr)` | execute from `oak_chunk_t*` or `CompileResult&` |
| `call(fn, args, out)` | call an `oak_value_t` function; `out` may be `nullptr` |
| `resume()` | resume after a paused execution |
| `set_module_registry(reg)` | attach a `ModuleRegistry` or raw `oak_module_registry_t*` |
| `set_user_data(p)` / `user_data()` | arbitrary pointer, surfaced as `Context::user_data()` |
| `raw()` | `oak_vm_t*` |

All `run`/`call`/`resume` methods return `oak_vm_result_t`
(`OAK_VM_OK` / `OAK_VM_RUNTIME_ERROR`).

### `oak::ModuleRegistry`

Holds compiled modules for multi-file programs. Attach one to a `VM` so
`import` statements can resolve at runtime.

| Method | Description |
|---|---|
| `get(id)` | `oak_module_t*` by numeric id |
| `find(path)` | `oak_module_t*` by file path |
| `raw()` | `oak_module_registry_t*` |

### Free functions

**`oak::compile`**

```cpp
oak::CompileResult oak::compile(const char* source, oak::CompileOptions& opts);
```

Lex, parse, and compile a source string. Returns a `CompileResult`; check
`ok()` before using the chunk.

**`oak::load_program`**

```cpp
oak::LoadResult oak::load_program(const char* path,
                                   oak::CompileOptions& opts,
                                   oak::ModuleRegistry& reg);
```

Load a multi-module program from `path`, resolve imports, and populate `reg`.
`LoadResult` mirrors `CompileResult`:

| Member | Description |
|---|---|
| `ok()` | true if the entry module loaded |
| `entry()` | `oak_module_t*` for the entry point |
| `error_count()` | number of diagnostics |
| `error(i)` | `Diagnostic` for error `i` |

### Complete binding example

```cpp
#include <oak.hpp>

struct Vec2 { float x, y; };

int main()
{
  oak::Allocator alloc;
  oak::CompileOptions opts(alloc);
  opts.with_stdlib();

  opts.bind_type<Vec2>("Vec2")
      .field("x", &Vec2::x)
      .field("y", &Vec2::y)
      .method("len", 0,
          [](oak::Context& ctx, oak::Args args) -> oak::Value {
            auto* v = static_cast<Vec2*>(args[0].as_native_instance());
            return oak::Value(std::sqrt(v->x * v->x + v->y * v->y));
          }, OAK_BIND_SCALAR(OAK_TYPE_NUMBER))
      .destructor();   // instances owned by Oak; freed with delete

  opts.bind_fn("make_vec2", 2,
      [](oak::Context& ctx, oak::Args args) -> oak::Value {
        auto* v = new Vec2{args[0].as_f32(), args[1].as_f32()};
        return oak::Value::from_raw(
            oak_native_record_new(ctx.allocator(), /* type */ nullptr, v));
      });

  auto result = oak::compile(
      "let v = make_vec2(3.0, 4.0);\n"
      "print(v.len());\n",   /* expects 5 */
      opts);

  if (!result.ok()) { return 1; }

  oak::VM vm(alloc);
  vm.run(result);
}
```

## Architecture

```
source -> lexer -> parser -> compiler -> bytecode -> VM
```
