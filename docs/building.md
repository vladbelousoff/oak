# Building and Installing

## Requirements

Required:

- Meson and Ninja
- a C17 compiler
- Python 3
- Git, for the wrapped `yyjson` dependency

Optional:

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
meson setup build
meson compile -C build
meson install -C build
oak examples/01_values/01_values.oak
```

By default, Oak installs under `~/.local`, so the executable lands in
`~/.local/bin`, the standard per-user binary directory used by most Linux
shells.

Pass `--prefix=<path>` to `meson setup` when packaging or when you need a
different install root. Installed builds find the stdlib relative to the `oak`
executable. Set `OAK_STDLIB_DIR` only to override that lookup.

## Web Playground

```sh
meson setup build_wasm --cross-file meson/cross/emscripten.ini
meson compile -C build_wasm oak_wasm.js
npm install
npm run dev
```

The build directory name matters: the Vite dev server serves the Emscripten
output straight out of `build_wasm/`.

For the static site, stage the runtime into Vite's public directory first —
the dev-server passthrough does not apply to production builds — then build:

```sh
mkdir -p www/public/build_wasm
cp build_wasm/oak_wasm.js build_wasm/oak_wasm.wasm www/public/build_wasm/
npm run build
```

The result lands in `_site/`.
