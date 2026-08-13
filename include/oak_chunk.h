#pragma once

#include "oak_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compiled bytecode.
 *
 * Opaque to embedders: a chunk is produced by oak_compile_ex() (owned by the
 * resulting oak_compile_result_t) or by the module loader (owned by the
 * oak_module_t), and is released with that owner.  It is never allocated or
 * freed directly.  Pass it to oak_vm_run(), which borrows it.
 *
 * The instruction set, constant pool and debug tables are internal and change
 * without notice; nothing here depends on their layout.
 */

typedef struct oak_chunk oak_chunk_t;

/* Print a human-readable listing of the chunk's instructions to stdout.
 * Requires a chunk compiled with oak_compile_options_t::emit_debug_info set
 * to see source lines and local names. */
OAK_API void oak_chunk_disassemble(const oak_chunk_t* chunk);

#ifdef __cplusplus
}
#endif
