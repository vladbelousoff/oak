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

## Code Conventions

- C17, built with Meson/Ninja
- Prefix all symbols (public API and internal compiler) with `oak_`
- `null` macro (not `NULL`), `u8`/`u16`/`u32`/`usize` typedefs from `oakc_defs.h`
- All heap allocation goes through `oak_allocator_t` (OAK_ALLOC / OAK_FREE macros)
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
