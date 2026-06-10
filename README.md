# Oak

Oak is a small, dynamically typed scripting language implemented in C17. It
compiles source to bytecode and executes it on a stack-based virtual machine.
The same runtime powers the native CLI, an embeddable C/C++ library, and a
WebAssembly playground.

Oak includes:

- numbers, strings, booleans, `none`, arrays, and maps
- functions, anonymous functions, records, enums, traits, and modules
- reference counting with automatic cycle collection and weak references
- native binding APIs for C and C++ hosts
- an interactive source debugger and bytecode disassembler

The project is currently source-only; there are no packaged releases or
installation targets.

## Quick Start

Prerequisites:

- Meson and Ninja
- a C17 compiler and a C++20 compiler
- Git, so Meson can obtain the wrapped `yyjson` dependency when needed

Build and run the first example:

```sh
meson setup build
meson compile -C build
./build/oak examples/01_values/01_values.oak
```

Run the complete test suite:

```sh
meson test -C build
```

For a debug build with runtime memory tracking and debug logging enabled:

```sh
meson setup build-debug --buildtype=debug
meson compile -C build-debug
meson test -C build-debug
```

## CLI

```text
oak [options] <script> [script args...]
```

Options must appear before the script path. Arguments after the script path are
not parsed as Oak CLI options.

| Option | Effect |
|---|---|
| `--debug` | Start the interactive debugger |
| `--disassemble` | Print each compiled module's bytecode instead of running |
| `--no-debug-symbols` | Compile without source-level debug metadata |
| `--track-memory` | Return a failure if tracked runtime allocations leak |
| `--help` | Print command usage |

The debugger stops before the first source line. Type `help` at its prompt to
list commands such as `step`, `next`, `finish`, `break`, `locals`, `print`,
`backtrace`, and `continue`.

```sh
./build/oak --debug examples/04_functions/04_functions.oak
./build/oak --disassemble examples/06_modules/06_modules.oak
./build/oak --track-memory examples/10_traits/10_traits.oak
```

## Learn The Language

The numbered programs in [`examples/`](examples/README.md) form the language
tour. They cover values, control flow, collections, functions, records, enums,
modules, file I/O, diagnostics, traits, weak references, and anonymous
functions. Meson also runs them as smoke tests.

A small Oak program looks like this:

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

Important syntax and runtime behavior:

- bindings are immutable by default; use `let mut` for reassignment
- strings use single quotes; double-quoted strings are rejected
- functions and methods may annotate parameter and return types
- records are constructed with `new Type { ... }`
- arrays and maps are mutable through indexing and methods
- `/` produces a float, while `//` performs integer division
- integers are signed 32-bit values and `+`, `-`, and `*` wrap on overflow
- `none` represents an empty value

See [`docs/traits.md`](docs/traits.md) for trait semantics and dispatch details.

## Modules

Imports use dotted module names resolved relative to the entry script. A module
named `analytics.stats` is loaded from `analytics/stats.oak`.

```oak
import { sum, average as mean } from analytics.stats;
import * from domain.project;
```

All top-level declarations are public. The loader rejects import cycles and
reports diagnostics from every module involved in a load.

## Standard Library

Core globals and methods are available without an import:

| API | Purpose |
|---|---|
| `print(value)` | Print a value |
| `to_int(value)`, `to_float(value)` | Convert a number |
| `is_int(value)`, `is_float(value)` | Inspect numeric storage |
| `.size()` | Return an array, map, or string length |
| array `.push(value)` | Append and return the new size |
| map `.has(key)`, `.delete(key)` | Query or remove a key |
| string `.format(values)` | Replace `{}` or `{n}` placeholders |

Math functions live in the `math` module:

```oak
import { sqrt, min, max } from math;
print(sqrt(max(16, 9)));
```

The `io` module exposes native file access:

```oak
import * from io;

let file = File.open('notes.txt', FileMode.Read);
let contents = file.read_all();
file.close();
print(contents);
```

`File` also provides `read()`, `write(value)`, `eof()`, and `close()`.

## Embedding Oak

The shared library target is named `acorn`. Public C headers live in
[`include/`](include/), and [`include/oak.hpp`](include/oak.hpp) provides a
header-only C++20 wrapper. The C API exposes the complete compilation and
execution pipeline:

1. Create an `oak_allocator_t`.
2. Initialize `oak_compile_options_t` and register the standard library or
   native bindings.
3. Load a program with `oak_module_loader_load_program()`, or lex, parse, and
   compile an in-memory source string.
4. Initialize `oak_vm_t` and run the compiled chunk.
5. Free results, registries, options, and the allocator in reverse order.

Native functions, records, value types, enums, and attributes are registered
through [`include/oak_bind.h`](include/oak_bind.h). Working examples are in
[`src/stdlib/`](src/stdlib/) and the compiler binding tests under
[`tests/compiler/`](tests/compiler/).

Minimal C++ execution:

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

The C++ wrapper owns runtime resources with RAII and supports native bindings
through `oak::CompileOptions`.

## Web Playground

The browser playground builds the runtime with Emscripten and serves it with
Vite. In addition to the native prerequisites, it requires Emscripten, Node.js,
and npm.

```sh
meson setup build-wasm --cross-file meson/cross/emscripten.ini
meson compile -C build-wasm
npm install
npm run dev
```

Use `npm run build` to produce the static site in `dist/`.

## Development

Useful commands:

```sh
meson compile -C build
meson test -C build
meson test -C build compiler_traits
meson test -C build --print-errorlogs
```

Repository layout:

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

The execution pipeline is:

```text
source -> lexer -> parser -> compiler -> bytecode -> VM
```
