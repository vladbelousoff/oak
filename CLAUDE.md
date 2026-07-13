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

Test files live under `tests/` as C harnesses using `oak_test_pipeline.h`. Each test compiles and optionally runs an Oak snippet, checking for success or expected errors.

```sh
meson test -C build                          # all tests
meson test -C build compiler_traits          # single suite
```

## Native Bindings

Register native types, functions, enums, and attributes on `oak_compile_options_t` before calling `oak_compile_ex()`. See `README.md` and `src/stdlib/` for examples.
