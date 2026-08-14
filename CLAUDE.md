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
  `oak_assert`. `OAK_BUILDING_ACORN`/`OAK_STATIC` in `oak_export.h` are the
  deliberate exceptions.

The `public_api` test enforces the first: `tests/public_api/oak_embed_smoke.c`
builds against `acorn_dep` with no `include_directories` and no `-DOAK_*`, so
it fails if either invariant breaks. A CI step greps for the second. Adding a
header to `include/` also means adding it to `oak_public_headers` in
`meson.build`, which is what `install_headers` ships.

## Code Conventions

- C17, built with Meson/Ninja
- Prefix all symbols (public API and internal compiler) with `oak_`
- `null` macro (not `NULL`), `u8`/`u16`/`u32`/`usize` typedefs from `oakc_defs.h`
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

Register native types, functions, enums, and attributes on `oak_compile_options_t` before calling `oak_compile_ex()`. See `README.md` and `src/stdlib/` for examples.
