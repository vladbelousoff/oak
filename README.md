# Oak

Oak is a small, dynamically typed scripting language implemented in C17. It
compiles source to bytecode and executes it on a stack-based virtual machine.
One runtime powers three frontends: a native CLI, an embeddable C/C++ library,
and a WebAssembly playground.

Language and runtime features:

- numbers, strings, booleans, `none`, arrays, and maps
- functions, anonymous functions, records, enums, traits, and modules
- reference counting with automatic cycle collection and weak references
- native binding APIs for C and C++ hosts
- an interactive source debugger and a bytecode disassembler

**Status:** version 1.0.0, source-only. There are no packaged releases and no
install target; build from this repository.

## Contents

- [Requirements](#requirements)
- [Build and Run](#build-and-run)
- [CLI Reference](#cli-reference)
- [The Language](#the-language)
- [Modules](#modules)
- [Standard Library](#standard-library)
- [Embedding Oak](#embedding-oak)
- [Web Playground](#web-playground)
- [Repository Layout](#repository-layout)

## Requirements

| Component | Needed for |
|---|---|
| Meson + Ninja | all builds |
| C17 compiler | the runtime and CLI |
| C++20 compiler | the C++ wrapper tests and C++ embedding |
| Python 3 | Meson build scripts |
| Git | fetching the wrapped `yyjson` subproject on first setup |
| Emscripten, Node.js, npm | the web playground only |

GCC, Clang, and MSVC are supported.

## Build and Run

```sh
meson setup build
meson compile -C build
./build/oak examples/01_values/01_values.oak
```

Run the test suite (the examples also run as smoke tests):

```sh
meson test -C build                  # everything
meson test -C build compiler_traits  # one suite
meson test -C build --print-errorlogs
```

For a debug build with runtime memory tracking and debug logging:

```sh
meson setup build-debug --buildtype=debug
meson compile -C build-debug
meson test -C build-debug
```

The build produces the `oak` CLI executable and the `acorn` runtime library
(shared on native targets, static under Emscripten).

## CLI Reference

```text
oak [options] <script> [script args...]
```

Parsing rules — the CLI is strict and fails fast on anything it does not
recognize:

- Options must appear **before** the script path. Everything after the script
  path is passed to the script verbatim, never parsed as CLI options.
- Only long options are accepted. Short options (`-d`) and option values
  (`--option=value`) are errors, as are unknown options.
- A bare `--` ends option parsing; the next argument is the script path.
- A script path is required unless `--help` is given.

| Option | Effect |
|---|---|
| `--debug` | Start the interactive debugger |
| `--disassemble` | Print each compiled module's bytecode instead of running |
| `--no-debug-symbols` | Compile without source-level debug metadata |
| `--track-memory` | Exit with failure if tracked runtime allocations leak |
| `--help` | Print usage and exit |

```sh
./build/oak --debug examples/04_functions/04_functions.oak
./build/oak --disassemble examples/06_modules/06_modules.oak
./build/oak --track-memory examples/10_traits/10_traits.oak
```

### Debugger

`--debug` stops before the first source line and opens a prompt. Type `help`
there to list all commands; the core set is `step`, `next`, `finish`, `break`,
`locals`, `print`, `backtrace`, and `continue`.

## The Language

A small Oak program:

```oak
fn sum(values : number[]) -> number {
  let mut total = 0;
  for value in values {
    total += value;
  }
  return total;
}

record Report {
  name : string;
  values : number[];
}

fn Report.total(self) -> number {
  return sum(self.values);
}

let report = new Report {
  name: 'weekly',
  values: [3, 5, 8, 13],
};

print(report.name);
print(report.total());
```

Rules worth knowing up front:

- Bindings are immutable by default; use `let mut` for reassignment.
- Strings use **single quotes**. Double-quoted strings are a lexer error.
- Functions and methods may annotate parameter and return types; the compiler
  checks them.
- Records are constructed with `new Type { ... }`.
- Arrays and maps are mutable through indexing and methods.
- `/` always produces a float; `//` performs integer division.
- Integers are signed 32-bit values; `+`, `-`, and `*` wrap on overflow.
- `none` represents an empty value.

### Language tour

The numbered programs in [`examples/`](examples/README.md) are the canonical
tour, in reading order: values, control flow, collections, functions, records
and enums, modules, algorithms, file I/O, diagnostics, traits, weak
references, and anonymous functions. Each is a runnable script and part of the
test suite, so they cannot drift out of date.

Trait semantics and dispatch are specified in
[`docs/traits.md`](docs/traits.md).

## Modules

Imports use dotted module names resolved **relative to the entry script**. A
module named `analytics.stats` is loaded from `analytics/stats.oak`.

```oak
import { sum, average as mean } from analytics.stats;
import * from domain.project;
```

- All top-level declarations are public; there is no export keyword.
- Import cycles are rejected at load time.
- Diagnostics are reported from every module involved in a load, not just the
  entry script.

## Standard Library

Core globals and methods are available without an import:

| API | Purpose |
|---|---|
| `print(value)` | Print a value |
| `to_int(value)`, `to_float(value)` | Convert a number |
| `is_int(value)`, `is_float(value)` | Inspect numeric storage |
| `.size()` | Length of an array, map, or string |
| array `.push(value)` | Append; returns the new size |
| map `.has(key)`, `.delete(key)` | Query or remove a key |
| string `.format(values)` | Replace `{}` or `{n}` placeholders |

Math functions live in the `math` module:

```oak
import { sqrt, min, max } from math;
print(sqrt(max(16, 9)));
```

File access lives in the `io` module:

```oak
import * from io;

let file = File.open('notes.txt', FileMode.Read);
let contents = file.read_all();
file.close();
print(contents);
```

`File` also provides `read()`, `write(value)`, `eof()`, and `close()`.

## Embedding Oak

Link against the `acorn` library. Public C headers live in
[`include/`](include/); [`include/oak.hpp`](include/oak.hpp) is a header-only
C++20 wrapper over the same API.

### C API

The C API exposes the full pipeline. The required order is:

1. Create an `oak_allocator_t`.
2. Initialize `oak_compile_options_t`; call `oak_stdlib_register()` and/or
   register your own native bindings ([`include/oak_bind.h`](include/oak_bind.h)).
3. Load a program with `oak_module_loader_load_program()`, or lex, parse, and
   compile an in-memory source string.
4. Initialize `oak_vm_t` and run the compiled chunk.
5. Free results, registries, options, and the allocator **in reverse order of
   creation**.

Working examples: [`src/stdlib/`](src/stdlib/) and the binding tests under
[`tests/compiler/`](tests/compiler/).

### C++ API

Minimal compile-and-run:

```cpp
#include <oak.hpp>

oak::Allocator allocator;
oak::CompileOptions options(allocator);
options.with_stdlib();

auto result = oak::compile("print('hello from C++');", options);
if (!result.ok())
  return 1;

oak::VM vm(allocator);
return vm.run(result) == OAK_VM_OK ? 0 : 1;
```

The wrapper owns runtime resources with RAII. Its main entry points:

- `oak::parse()` and `oak::walk()` — parsing and read-only AST traversal
- `oak::compile()` and `oak::CompileOptions` — compilation and native bindings
- `oak::VM`, `oak::Value`, `oak::Args` — execution and native calls
- `oak::ModuleRegistry` and `oak::load_program()` — multi-module programs

Most wrappers expose `raw()` for direct access to the underlying C object.

Parsing and AST traversal (for tools such as transpilers):

```cpp
oak::Allocator allocator;
auto parsed = oak::parse("let answer = 40 + 2;", allocator);
if (!parsed.ok())
  return 1;

oak::walk(parsed.root(), [](oak::AstNode node) {
  if (node.is_terminal())
    std::printf("%s: %.*s\n", node.kind_name(), (int)node.text().size(),
                node.text().data());
});
```

`oak::ParseResult` owns the parser and lexer storage; `oak::AstNode` views are
valid only until their parse result is destroyed. Nodes expose `children()`,
`child()`, token text, source locations, and `raw()`.

### C++ Native Bindings

Register global functions with `CompileOptions::bind_fn()`:

```cpp
options.bind_fn("add", [](int a, int b) { return a + b; });
```

Typed bindings infer arity, parameter types, conversions, and return types for
`int`, `float`, `bool`, and `void`. Once `bind_type<T>()` has registered a
native record, typed functions may also accept it as `T*`, `T&`, or
`const T&`.

Records can expose C++ member fields and bind member functions directly:

```cpp
struct Vec2 {
  float x, y;
  float length_squared() const { return x * x + y * y; }
  float dot(const Vec2& other) const { return x * other.x + y * other.y; }
};

auto vec2 = options.bind_type<Vec2>("Vec2");
vec2.field("x", &Vec2::x)
    .field("y", &Vec2::y)
    .method("length_squared", &Vec2::length_squared)
    .method("dot", &Vec2::dot)
    .destructor();

options.bind_fn("vec_sum", [](const Vec2& v) { return v.x + v.y; });
```

For cases the typed layer cannot express — VM context access, dynamic values,
custom return types, manual conversion — use the explicit-arity `bind_fn()`,
`method()`, and `static_method()` overloads taking `(Context&, Args) -> Value`
callbacks; method callbacks receive the record instance as argument zero. Use
`bind_enum()` for native enums, or `bind_fn_raw()` and explicit field
accessors when the C callback API is preferable.

**Lifetime rules** — violating either is undefined behavior:

- Bindings borrow name strings and store C++ callbacks inside
  `CompileOptions`. The options object and every borrowed string must outlive
  all chunks compiled with them **and** their VM execution.
- The no-argument `destructor()` calls `delete` on a `T*`. If instances use
  any other allocation strategy, provide a custom destructor.

## Web Playground

The playground builds the runtime with Emscripten and serves it with Vite:

```sh
meson setup build-wasm --cross-file meson/cross/emscripten.ini
meson compile -C build-wasm
npm install
npm run dev       # local dev server
npm run build     # static site in dist/
```

## Repository Layout

| Path | Contents |
|---|---|
| `oak.c`, `oak_cli.c` | Native CLI |
| `include/` | Public C API and C++ wrapper |
| `src/lexer/` | Tokenization |
| `src/parser/` | AST construction |
| `src/compiler/` | Type checking and bytecode emission |
| `src/runtime/`, `src/vm/` | Values, memory management, and execution |
| `src/stdlib/`, `stdlib/` | Native implementations and Oak module stubs |
| `tests/` | C/C++ test executables |
| `examples/` | Runnable language tour |
| `wasm/`, `www/` | WebAssembly entry point and playground UI |

Execution pipeline:

```text
source -> lexer -> parser -> compiler -> bytecode -> VM
```
