# Oak

Oak is a scripting language implemented in C17. It compiles `.oak` source to
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
and source debugging.

The debugger assumes `oak` is on the `PATH` inherited by VS Code. The
contributed **Debug Current Oak File** configuration runs the active `.oak`
file from that file's directory:

```json
{
  "type": "oak",
  "request": "launch",
  "name": "Debug Current Oak File",
  "program": "${file}",
  "cwd": "${fileDirname}",
  "args": []
}
```

To run the extension from this checkout:

```sh
code editors/vscode
```

Select **Run Extension** and press `F5`. In the Extension Development Host,
open a `.oak` file, select **Debug Current Oak File**, and press `F5`.

To attach instead:

```sh
oak --debug --debug-port 4711 path/to/program.oak
```

Then attach to port `4711` with an `oak` attach configuration.

## Language

Start with [`examples/`](examples/README.md). The numbered examples are the
language tour and run as smoke tests.

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

Rules worth knowing:

- bindings are immutable unless declared with `let mut`
- strings use single quotes
- functions and methods can declare parameter and return types
- records are created with `new Type { ... }`
- imports resolve relative to the entry script
- `/` produces a float; `//` is integer division
- `none` is the empty value

Common globals include `print`, numeric conversions, math functions, and
collection/string methods such as `.size()`. File access is in the `io` module.

## Embedding

Link against `acorn`. Public C headers are in [`include/`](include/), and the
C++20 wrapper is [`include/oak.hpp`](include/oak.hpp).

See [`tests/compiler/`](tests/compiler/) and [`src/stdlib/`](src/stdlib/) for
native binding examples.

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
| **oak** | 14.52× (0.15 s) | 16.74× (0.12 s) | 47.43× (0.37 s) | 5.57× (0.26 s) | 12.25× (0.37 s) |
| lua5.4 | 5.44× (0.06 s) | 3.51× (0.02 s) | 11.50× (0.09 s) | 3.14× (0.15 s) | 7.86× (0.24 s) |
| luajit | 1.00× (0.01 s) | 1.00× (0.01 s) | 1.00× (0.01 s) | 1.00× (0.05 s) | 1.00× (0.03 s) |
| python3 | 11.63× (0.12 s) | 11.72× (0.08 s) | 112.58× (0.88 s) | 6.66× (0.31 s) | 13.74× (0.42 s) |
| ruby | 14.93× (0.15 s) | 15.36× (0.11 s) | 36.17× (0.28 s) | 5.90× (0.27 s) | 11.27× (0.34 s) |
| node | 4.02× (0.04 s) | 5.42× (0.04 s) | 5.05× (0.04 s) | 2.21× (0.10 s) | 1.16× (0.03 s) |
| csharp | 1.97× (0.02 s) | 2.45× (0.02 s) | 4.16× (0.03 s) | 3.44× (0.16 s) | 6.83× (0.21 s) |

_Relative to the fastest runtime per benchmark, lower is better; median wall time in parentheses. Measured on a GitHub-hosted `ubuntu-latest` runner at `90bf34475` on 2026-07-03. LuaJIT, Node, and mono are JIT-compiled reference points._
<!-- benchmark:end -->

## Layout

| Path | Contents |
|---|---|
| `oak.c`, `oak_cli.c` | native CLI |
| `include/` | public C API and C++ wrapper |
| `src/` | compiler, runtime, VM, and stdlib C code |
| `stdlib/` | Oak stdlib modules |
| `tests/` | C/C++ tests |
| `examples/` | runnable language tour |
| `editors/vscode/` | VS Code extension |
| `wasm/`, `www/` | WebAssembly playground |
