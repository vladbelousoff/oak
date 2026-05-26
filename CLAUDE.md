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
- **VM** (`src/vm/`): stack-based bytecode interpreter operating on 16-byte `oak_value_t` tagged unions

Public headers live in `include/`. Internal compiler headers are in `src/compiler/internal/`.

## Code Conventions

- C17, built with Meson/Ninja
- Prefix public API with `oak_`, internal compiler symbols with `oakc_`
- `null` macro (not `NULL`), `u8`/`u16`/`u32`/`usize` typedefs from `oakc_defs.h`
- All heap allocation goes through `oak_allocator_t` (OAK_ALLOC / OAK_FREE macros)
- Values are reference-counted (`oak_value_t`); weak references use `is_weak` flag

## Key Design Notes

- Oak strings are **single-quoted** (`'hello'`). Double-quoted strings are not supported and cause hangs.
- The parser grammar is **declarative**: rules are SEQUENCE, CHOICE, BINARY, UNARY, TOKEN, PRATT entries in a table. CHOICE nodes are transparent (they return the matched child directly, not a wrapper).
- **Generics use type erasure**, not monomorphization. All values share the same 16-byte representation, so a single bytecode body works for all instantiations. No VM changes needed.
- Type parameter IDs occupy a reserved range: `OAK_TYPE_PARAM_BASE (0x70000000) + index`. Max 8 type params per definition.
- Generic param names must be **null-terminated heap copies** — token text from the lexer is not null-terminated.
- The compiler uses `c->generic_params` / `c->generic_param_count` for the active generic context; these must be saved/restored around generic body compilation and call-site type checking.

## Tests

Test files live under `tests/` as C harnesses using `oak_test_pipeline.h`. Each test compiles and optionally runs an Oak snippet, checking for success or expected errors.

```sh
meson test -C build                          # all tests
meson test -C build compiler_generics        # single suite
```

## Native Bindings

Register native types, functions, enums, and attributes on `oak_compile_options_t` before calling `oak_compile_ex()`. See `README.md` and `src/stdlib/` for examples.
