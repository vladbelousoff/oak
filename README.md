# oak

oak is a scripting language implemented in C17. It compiles `.oak` source to
bytecode and runs it on a stack-based virtual machine.

The repo includes the `oak` CLI, the `acorn` C/C++ embedding library, a VS Code
extension, examples, tests, and a WebAssembly playground.

## Requirements

Required:

- Meson and Ninja
- a C17 compiler
- Python 3
- Git, for the wrapped `yyjson` dependency

Optional:

- a C++20 compiler for C++ wrapper tests and C++ embedding
- Emscripten, Node.js, and npm for the web playground

GCC, Clang, and MSVC are supported.

## Build and Test

```sh
meson setup build
meson compile -C build
./build/oak examples/01_values/01_values.oak
```

On Windows, run `.\build\oak.exe` instead of `./build/oak`.

```sh
meson test -C build
meson test -C build --print-errorlogs
```

For a debug build:

```sh
meson setup build-debug --buildtype=debug
meson compile -C build-debug
meson test -C build-debug
```

## Install

```sh
meson setup build --prefix="$HOME/.local"
meson compile -C build
meson install -C build
```

Add `<prefix>/bin` to `PATH`. Installed builds find the stdlib relative to the
`oak` executable. Set `OAK_STDLIB_DIR` only to override that lookup.

## Run

```text
oak [options] <script> [script args...]
```

Options come before the script path. Everything after the script path is passed
to the script unchanged.

| Option | Effect |
|---|---|
| `--debug` | Start the localhost VS Code/DAP debugger server |
| `--debug-port <port>` | Debug server port; use `0` to choose a free port |
| `--disassemble` | Print compiled bytecode instead of running |
| `--no-debug-symbols` | Compile without source debug metadata |
| `--track-memory` | Fail if tracked runtime allocations leak |
| `--help` | Print usage |

```sh
oak examples/01_values/01_values.oak
oak --disassemble examples/06_modules/06_modules.oak
oak --debug --debug-port 4711 examples/04_functions/04_functions.oak
```

## VS Code

The extension is in `editors/vscode`. It provides `.oak` syntax highlighting
and source debugging. The debugger assumes `oak` is on the `PATH` inherited by
VS Code.

```sh
code editors/vscode
```

Select **Run Extension** and press `F5`. To attach to a running script:

```sh
oak --debug --debug-port 4711 path/to/program.oak
```

## Language

Start with [`examples/`](examples/README.md). The numbered examples are the
language tour and run as smoke tests. A compact oak program looks like this:

```oak
fn sum(values : number[]) -> number {
  let mut total = 0;
  for value in values {
    total += value;
  }
  return total;
}

print(sum([3, 5, 8]));
```

Feature highlights:

- typed values: numbers, strings, booleans, arrays, maps, records, enums,
  traits, functions, and `none`
- immutable `let` bindings by default; `let mut` for mutation
- `if`, `while`, counted `for`, collection iteration, `break`, `continue`
- typed functions, recursion, first-class functions, anonymous functions
- records with fields and methods declared as `fn Type.method(self, ...)`
- enum variants such as `Status.Done`, with enum-aware type checking
- traits with virtual dispatch over implementing record methods
- relative modules with `import { name } from module.path` and `import *`
- weak references with `Type weak` to break ownership cycles
- compile-time cycle checking: reference counting alone reclaims every object,
  with no runtime cycle collector (fields that could close a strong reference
  cycle are write-once or must be declared weak)
- stdlib builtins for printing, conversion, math, strings, collections, and
  `io.File`
- bytecode disassembly, debug symbols, DAP debugging, and memory tracking

Rules worth remembering: strings use single quotes, `/` produces a float, `//`
is integer division, and records are created with `new Type { ... }`.

## Embedding

Link against `acorn`. Public C headers are in [`include/`](include/), and the
C++20 wrapper is [`include/oak.hpp`](include/oak.hpp).

The embedding API lets a host register native record types, inline value types,
fields, destructors, global functions, module-scoped functions, instance
methods, static methods, enums, and attributes before compiling oak source.
Native bindings participate in oak's compile-time type checks.

### C Bindings

The C API is descriptor-based. Register types and functions on
`oak_compile_options_t` before calling `oak_compile_ex`.

```c
#include "oak_bind.h"

typedef struct Vec2
{
  float x;
  float y;
} Vec2;

static struct oak_value_t vec2_get_x(struct oak_value_t self, void* user_data);
static void vec2_free(void* instance);
static enum oak_fn_call_result_t make_vec2(
    struct oak_native_ctx_t* ctx,
    const struct oak_value_t* args,
    int argc,
    struct oak_value_t* out);

static void register_host_api(struct oak_compile_options_t* opts)
{
  struct oak_bind_type_t* vec2 =
      oak_bind_type(opts, OAK_BIND_TYPE_RECORD, "Vec2");
  vec2->destructor = vec2_free;

  oak_bind_field(vec2,
                 &(struct oak_bind_field_t){
                     .name = "x",
                     .type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                     .getter = vec2_get_x,
                 });

  static struct oak_bind_type_ref_t params[2];
  params[0] = OAK_BIND_SCALAR(OAK_TYPE_NUMBER);
  params[1] = OAK_BIND_SCALAR(OAK_TYPE_NUMBER);

  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .name = "vec2",
                         .impl = make_vec2,
                         .arity = 2,
                         .return_type = OAK_BIND_NATIVE(vec2),
                         .param_types = params,
                         .param_count = 2,
                         .user_data = vec2,
                     });
}
```

Use `oak_bind_type_in_module`, `oak_bind_enum_in_module`, or the
`module_name` field on function descriptors for module-scoped native APIs.
Use `oak_bind_fn()` for instance or static methods, and `oak_bind_enum()` for
native enum bindings. Callbacks create records with `oak_native_record_new()`
and access instances with `oak_native_instance()`.

### C++ Bindings

The C++20 wrapper adds RAII and typed callable binding:

```cpp
#include "oak.hpp"

#include <cmath>

struct Vec2
{
  float x;
  float y;

  float length() const { return std::sqrt(x * x + y * y); }
  void scale(float factor)
  {
    x *= factor;
    y *= factor;
  }
  float dot(const Vec2& other) const { return x * other.x + y * other.y; }
};

oak::Allocator alloc;
oak::CompileOptions opts(alloc);

auto vec2 = opts.bind_type<Vec2>("Vec2");
vec2.field("x", &Vec2::x)
    .field("y", &Vec2::y)
    .method("length", &Vec2::length)
    .method("scale", &Vec2::scale)
    .method("dot", &Vec2::dot)
    .static_method("twice", [](int value) { return value * 2; })
    .destructor();

opts.bind_fn("add", [](int a, int b) { return a + b; });
opts.bind_fn("vec_sum", [](const Vec2* v) { return v->x + v->y; });
opts.bind_enum("Color", {{"Red", 0}, {"Green", 1}, {"Blue", 2}});

auto result = oak::compile(
    "let n = add(20, 22);\n"
    "let color = Color.Green;\n",
    opts);
```

For complete, compiled examples, see [`tests/compiler/`](tests/compiler/),
[`tests/cpp/test_oak_hpp.cpp`](tests/cpp/test_oak_hpp.cpp), and
[`src/stdlib/`](src/stdlib/).

## Web Playground

```sh
meson setup build-wasm --cross-file meson/cross/emscripten.ini
meson compile -C build-wasm
npm install
npm run dev
```

Use `npm run build` to create the static site in `dist/`.

## Benchmarks

Cross-language benchmarks run in CI whenever interpreter code changes and the
table below is updated automatically. Workloads, methodology, and caveats are
described in [`benchmark/`](benchmark/README.md).

<!-- benchmark:start -->
| runtime | fib | nsieve | mandelbrot | hashmap | strcat |
|---|---|---|---|---|---|
| **oak** | 2.45× (5.35 s) | 4.41× (5.88 s) | 3.91× (7.14 s) | 2.63× (6.06 s) | 3.72× (6.50 s) |
| lua5.4 | 1.00× (2.18 s) | 1.00× (1.34 s) | 1.00× (1.83 s) | 1.80× (4.15 s) | 2.89× (5.04 s) |
| python3 | 1.98× (4.32 s) | 2.58× (3.45 s) | 9.67× (17.66 s) | 3.09× (7.12 s) | 4.34× (7.58 s) |
| ruby | 1.70× (3.70 s) | 2.23× (2.98 s) | 2.38× (4.35 s) | 2.37× (5.46 s) | 3.39× (5.92 s) |
| perl | 7.83× (17.08 s) | 4.29× (5.73 s) | 4.63× (8.46 s) | 1.30× (2.99 s) | 1.00× (1.75 s) |
| php | 8.38× (18.27 s) | 3.43× (4.58 s) | 3.13× (5.72 s) | 1.00× (2.31 s) | 1.47× (2.56 s) |

_Relative to the fastest runtime per benchmark, lower is better; median wall time in parentheses. Measured on a GitHub-hosted `ubuntu-latest` runner at `0d9e3bea8` on 2026-07-10. All runtimes are bytecode interpreters (no JIT)._
<!-- benchmark:end -->

## Layout

| Path | Contents |
|---|---|
| `oak.c`, `oak_cli.c` | native CLI |
| `include/` | public C API and C++ wrapper |
| `src/` | compiler, runtime, VM, and stdlib C code |
| `stdlib/` | oak stdlib modules |
| `tests/` | C/C++ tests |
| `examples/` | runnable language tour |
| `editors/vscode/` | VS Code extension |
| `wasm/`, `www/` | WebAssembly playground |
