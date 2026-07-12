# Building and Installing

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

## Debug and Release Builds

For a debug build:

```sh
meson setup build-debug --buildtype=debug
meson compile -C build-debug
meson test -C build-debug
```

Meson's default buildtype is `debug`: -O0 with memory tracking compiled in,
which runs 4-8x slower than an optimized build. For anything
performance-sensitive (such as the [benchmark suite](../benchmark/README.md)),
build optimized:

```sh
meson setup build-release --buildtype=release
meson compile -C build-release
```

## Install

```sh
meson setup build --prefix="$HOME/.local"
meson compile -C build
meson install -C build
```

Add `<prefix>/bin` to `PATH`. Installed builds find the stdlib relative to the
`oak` executable. Set `OAK_STDLIB_DIR` only to override that lookup.

## Web Playground

```sh
meson setup build-wasm --cross-file meson/cross/emscripten.ini
meson compile -C build-wasm
npm install
npm run dev
```

Use `npm run build` to create the static site in `dist/`.
