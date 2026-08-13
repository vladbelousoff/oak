#pragma once

#include "oak_chunk.h"
#include "oak_container.h"
#include "oak_export.h"
#include "oak_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sentinel module_id used by native fns and the entry-only chunk before a
 * registry exists. */
#define OAK_MODULE_ID_NONE ((u16)0xFFFF)

typedef struct oak_allocator oak_allocator_t;

/*
 * A compiled module.
 *
 * Opaque: a module owns its source mapping, lexer, AST, bytecode, type
 * registry and export tables, and is always created and destroyed through the
 * registry below — never stack-allocated.  Read what an embedder needs with
 * the accessors further down.
 */
typedef struct oak_module oak_module_t;

/* Owns every module loaded for one program, indexed by id and by canonical
 * path.  Initialize it on the stack with oak_module_registry_init and hand it
 * to oak_module_loader_load_program (oak_module_loader.h), then attach it to a
 * VM with oak_vm_set_module_registry so cross-module calls resolve. */
typedef struct oak_module_registry oak_module_registry_t;
struct oak_module_registry
{
  oak_allocator_t* allocator;
  oak_container_t* modules;           /* vector of oak_module_t*, index = id */
  oak_container_t* by_canonical_path; /* path → usize module_id             */
};

OAK_API void oak_module_registry_init(oak_module_registry_t* reg,
                                      oak_allocator_t* allocator);

/* Free every module in the registry and the registry's own tables.  The
 * registry struct itself is the caller's (usually stack) storage. */
OAK_API void oak_module_registry_free(oak_module_registry_t* reg);

/* O(1) lookup. Returns null if no module with that id. */
OAK_API oak_module_t*
oak_module_registry_get(const oak_module_registry_t* reg, u16 module_id);

/* O(1) lookup by canonical path. Returns null if not present. */
OAK_API oak_module_t*
oak_module_registry_find_by_path(const oak_module_registry_t* reg,
                                 const char* canonical_path);

/* Allocate a module, append to registry, assign it a module_id. The strings
 * `canonical_path` and `dotted_name` are duplicated. */
OAK_API oak_module_t*
oak_module_registry_new(oak_module_registry_t* reg,
                        const char* canonical_path,
                        const char* dotted_name);

/* The module's bytecode, or null if it has not compiled successfully.  Owned
 * by the module; pass it to oak_vm_run, which borrows it. */
OAK_API oak_chunk_t* oak_module_chunk(const oak_module_t* mod);

/* Dotted name for diagnostics, e.g. "a.b.c".  Borrowed from the module. */
OAK_API const char* oak_module_dotted_name(const oak_module_t* mod);

/* Canonical filesystem path used as the registry key.  Borrowed. */
OAK_API const char* oak_module_path(const oak_module_t* mod);

/* Dense registry index; OAK_MODULE_ID_NONE for a null module. */
OAK_API u16 oak_module_id(const oak_module_t* mod);

/* Non-zero when this is the program's entry module. */
OAK_API int oak_module_is_entry(const oak_module_t* mod);

#ifdef __cplusplus
}
#endif
