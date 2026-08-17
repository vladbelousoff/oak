# Oak — Development Guide

## Build

```sh
meson setup build
meson compile -C build
meson test -C build
```

## Architecture

```
source → lexer → parser → compiler → bytecode → VM
```

- **Lexer** (`src/lexer/`): tokenizes source into `oak_token_t` stream
- **Parser** (`src/parser/`): declarative table-driven grammar producing an AST of `oak_ast_node_t`
- **Compiler** (`src/compiler/`): walks the AST, performs type checking, emits bytecode into `oak_chunk_t`
- **VM** (`src/vm/`): stack-based bytecode interpreter operating on 8-byte packed `oak_value_t` words (3-bit tag; objects are referenced by table id + slot index + nonce; each VM owns an object table, table 0 is shared for chunk constants and embedder objects)

Public headers live in `include/`. Internal compiler headers are in `src/compiler/internal/`.

### Packages

- **`src/package/`**: manifest (`oak.json`) and lockfile (`oak.lock`) parsing
  and writing, semver, the content-addressed cache layout, minimal version
  selection, sha256, process spawning, and the git driver. Built as the
  `oak_pkg` static library so `acorn`, `oak-pkg` and the test binary can all
  link it. **Nothing in it is `OAK_API`**, which is why linking it into both
  `acorn` and `oak_tests` is not a duplicate-symbol conflict.
- **`src/common/oak_package.c`** implements the public `oak_package.h`. It lives
  in `acorn` rather than `src/package/` precisely because it *is* exported.
- **`src/common/oak_module_mount.c`**: a mount is `(scope_root, ns, root_dir)`
  and claims the first dotted segment of an import. Longest matching scope
  wins, which is what makes a dependency's dependencies invisible to you. A
  mount is refused over a registered native module, so a package cannot shadow
  the stdlib.
- **`tools/oak-pkg/`**: the CLI. `oak_pkg_http.c` is the only file in the
  project that touches the network, and libcurl/libarchive reach no other
  target — the runtime never fetches, it reads a lockfile. HTTPS is optional
  (`-Dhttps=`); without it git and path dependencies still work completely.
- **Plugins**: `include/oak_plugin.h` is the ABI, `src/common/oak_plugin_host.c`
  loads them. A plugin's `bind` is an ordinary binding function, so everything
  downstream of it is unchanged. Load order matters: `oak_package_set_apply`
  mounts every package *before* loading any plugin, because binding makes a
  namespace a native module and a mount over one is refused.

`oak_export.h`'s `OAK_BUILDING_ACORN`/`OAK_STATIC` and `oak_plugin.h`'s
`OAK_PLUGIN_EXPORT` are the only public-header build-define exceptions; the
latter keys on `_WIN32` and the compiler alone, never on an `OAK_*` flag.

`include/` is the installed contract and holds two invariants, both guarded:

- **Self-contained.** No public header may include one from `src/`. A header
  with both public and private parts is split: the public one keeps the opaque
  handle and the operations an embedder needs, and an `_impl.h` next to the
  implementation holds the layout (`oak_value_impl.h`, `oak_chunk_impl.h`,
  `oak_module_impl.h`). Never give a private header the same basename as a
  public one.
- **No build defines.** No public header may key on `OAK_TRACK_MEMORY`,
  `OAK_DEBUG_LOGGING` or `OAK_ATOMIC_REFCOUNT` — a consumer never passes them,
  so anything conditional on one is an ABI trap or a silent behaviour change.
  Keep the conditional in a private header and expose only the
  configuration-independent type (see `oak_refcount.h` vs
  `src/common/oak_refcount_ops.h`). Public inline code uses `<assert.h>`, not
  `OAK_ASSERT`. `OAK_BUILDING_ACORN`/`OAK_STATIC` in `oak_export.h` are the
  deliberate exceptions.

The `public_api` test enforces the first: `tests/public_api/oak_embed_smoke.c`
builds against `acorn_dep` with no `include_directories` and no `-DOAK_*`, so
it fails if either invariant breaks. A CI step greps for the second. Adding a
header to `include/` also means adding it to `oak_public_headers` in
`meson.build`, which is what `install_headers` ships.

## Code Conventions

- C17, built with Meson/Ninja
- Prefix all symbols (public API and internal compiler) with `oak_`
- `OAK_NULL` macro (not `NULL`), `u8`/`u16`/`u32`/`usize` typedefs from `oakc_defs.h`
- All heap allocation goes through `oak_allocator_t`: `oak_alloc`,
  `oak_realloc` and `oak_free`, each taking an explicit `oak_source_loc_t`.
  Pass `OAK_HERE` when this call site is where the memory was asked for. When
  it is not — a helper allocating on a caller's behalf — take an
  `oak_source_loc_t` parameter and forward it, so the tracking allocator blames
  the caller rather than the helper. There is deliberately no allocation macro:
  a macro can only ever name its own expansion site.
- Values are reference-counted (`oak_value_t`); weak references use `is_weak` flag
- There is no runtime cycle collector: the compiler rejects programs that could
  form strong reference cycles (`src/compiler/oak_compiler_cycles.c`). Native
  bindings must uphold the same invariant — never create a strong ownership
  loop from C code (use weak values or restructure ownership).

## Key Design Notes

- Oak strings are **single-quoted** (`'hello'`). Double-quoted strings are not supported and produce a lexer error.
- The parser grammar is **declarative**: rules are SEQUENCE, CHOICE, BINARY, UNARY, TOKEN, PRATT entries in a table. CHOICE nodes are transparent (they return the matched child directly, not a wrapper).

## Tests

Tests live in `tests/suites/<name>_test.c`, built on vendored
[utest.h](../tests/third_party/utest.h). Every suite declares `OAK_TEST_SUITE(<name>)`
once, then writes tests as `UTEST_F(<name>, what_it_does)`; the fixture supplies a
per-test tracking allocator (`OAK_A`) whose teardown fails the test on a leak.
`tests/support/oak_test_support.h` drives the lex/parse/compile/run pipeline and
provides the table-driven `OAK_EXPECT_*_CASES` macros.

All suites link into one `oak_tests` binary; meson registers each one via
`--filter`, so the suite name must match the filename stem. Tests do no
filesystem I/O — output is captured through in-process pipes.

```sh
meson test -C build                          # all tests
meson test -C build compiler_interfaces      # single suite
./build/oak_tests --list-tests               # every test in the binary
./build/oak_tests --filter='parser.*'        # run a suite directly
```

## Native Bindings

Register native types, functions, enums, and attributes on `oak_compile_options_t` before calling `oak_compile_ex()`. See `docs/embedding-c.md` and `src/stdlib/` for examples.

A plugin is the same code in a shared library (`docs/publishing.md`). Bumping
`OAK_PLUGIN_ABI` is required whenever `oak_plugin_t` or the expectations around
`bind` change — a plugin built against a different number is refused, not called.
