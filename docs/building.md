# Building and installing

## What you need

- Meson and Ninja
- a C17 compiler (GCC, Clang, or MSVC)
- Python 3
- Git, so Meson can fetch the wrapped `yyjson` dependency

For the web playground you also need Emscripten, Node.js, and npm.

## Build and run

```sh
meson setup build
meson compile -C build
./build/oak examples/01_values/01_values.oak
```

On Windows, run `.\build\oak.exe` instead of `./build/oak`.

## Tests

```sh
meson test -C build
meson test -C build --print-errorlogs
```

To run one suite, pass its name: `meson test -C build parser`.

## Debug vs release

`meson setup build` defaults to a debug build: no optimization, and
memory tracking compiled in. That is fine for development and about
4–8× slower than an optimized build.

For anything speed-sensitive:

```sh
meson setup build-release --buildtype=release
meson compile -C build-release
```

A named debug directory is the same idea:

```sh
meson setup build-debug --buildtype=debug
meson compile -C build-debug
```

## Install

```sh
meson setup build
meson compile -C build
meson install -C build
oak examples/01_values/01_values.oak
```

The default prefix is `~/.local`, so the binary lands in `~/.local/bin`.
Pass `--prefix=<path>` to `meson setup` to put it somewhere else.

`oak-pkg`, the package manager, installs alongside it.

### Build options

| Option | Default | Effect |
|---|---|---|
| `-Dinstall=` | `true` | Install the CLI and runtime library |
| `-Dpkg=` | `true` | Build `oak-pkg`. Off for cross builds; `oak` still reads `oak.lock` either way |
| `-Dhttps=` | `auto` | Archive dependencies over HTTPS, via libcurl and libarchive. Without them, `oak-pkg` handles git and path dependencies completely |
| `-Datomic_refcount=` | `false` | Atomic reference counting; only for embedders that share objects across threads |

Neither libcurl nor libarchive reaches `acorn`, the `oak` CLI, or the
WebAssembly build — only `oak-pkg` links them, because the runtime never
fetches anything. They come from wrapdb if the system has no copy.

An installed `oak` finds the stdlib next to the executable. Set
`OAK_STDLIB_DIR` only if you need to override that.

### Using Oak from another C program

Install also ships the public headers under `<prefix>/include/oak` and a
pkg-config file. If Oak is not in a system prefix, tell pkg-config where
to look:

```sh
export PKG_CONFIG_PATH=<prefix>/lib/pkgconfig
```

`--cflags` gives you `-I<prefix>/include/oak`, so
`#include "oak_program.h"` works the same in-tree and installed.
The API itself is in [embedding-c.md](embedding-c.md).

## Web playground

```sh
meson setup build_wasm --cross-file meson/cross/emscripten.ini
meson compile -C build_wasm oak_wasm.js
npm install
npm run dev
```

The directory name matters: the Vite dev server serves the Emscripten
output from `build_wasm/`.

For a static site, copy the runtime into Vite's public directory first,
then build:

```sh
mkdir -p www/public/build_wasm
cp build_wasm/oak_wasm.js build_wasm/oak_wasm.wasm www/public/build_wasm/
npm run build
```

The result lands in `_site/`.
