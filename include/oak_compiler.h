#pragma once

#include "oak_chunk.h"
#include "oak_diagnostic.h"
#include "oak_export.h"
#include "oak_parser.h"

/* Result of compiling an Oak program.
 *
 * `chunk` is the emitted bytecode, owned by the result; pass the whole
 * struct to oak_compile_result_free() to release it.  On compile failure
 * `chunk` is NULL and `errors[0..error_count]` describe what went wrong.
 * Diagnostics are stored inline (no heap), capped at OAK_MAX_DIAGNOSTICS. */
struct oak_compile_result_t
{
  struct oak_chunk_t* chunk; /* NULL on failure */
  struct oak_diagnostic_t errors[OAK_MAX_DIAGNOSTICS];
  int error_count;
};

/* Compile a parsed AST into bytecode using the default allocator and no
 * native bindings.  For native types/fns or a custom allocator, use
 * oak_compile_ex (declared in oak_bind.h).
 * The caller retains ownership of `root` (the AST is not freed by oak_compile).
 * `out` must be initialized by the caller; oak_compile populates it and the
 * caller releases it with oak_compile_result_free. */
OAK_API void oak_compile(const struct oak_ast_node_t* root,
                         struct oak_compile_result_t* out);

/* Free the chunk and any heap-owned diagnostic state inside `result`.
 * Safe to call on a zero-initialized or already-failed result. */
OAK_API void oak_compile_result_free(struct oak_compile_result_t* result);

/* oak_compile_ex is declared in oak_bind.h (requires oak_compile_options_t). */
