#pragma once

#include "oak_chunk.h"
#include "oak_diagnostic.h"
#include "oak_export.h"
#include "oak_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Result of compiling an Oak program.
 *
 * `chunk` is the emitted bytecode, owned by the result; pass the whole
 * struct to oak_compile_result_free() to release it.  On compile failure
 * `chunk` is NULL and `errors[0..error_count]` describe what went wrong.
 * Diagnostics are stored inline (no heap), capped at OAK_MAX_DIAGNOSTICS. */
typedef struct oak_compile_result oak_compile_result_t;
struct oak_compile_result
{
  oak_chunk_t* chunk; /* NULL on failure */
  oak_diagnostic_t errors[OAK_MAX_DIAGNOSTICS];
  int error_count;
};

/* Free the chunk and any heap-owned diagnostic state inside `result`, and null
 * what it releases, so calling it twice is a no-op rather than a double free.
 * Safe on a zero-initialized or already-failed result. */
OAK_API void oak_compile_result_free(oak_compile_result_t* result);

/* Compilation itself is oak_compile_ex(), declared in oak_bind.h because it
 * takes an oak_compile_options_t. */

#ifdef __cplusplus
}
#endif
