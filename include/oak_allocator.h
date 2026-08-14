#pragma once

#include "oak_export.h"
#include "oak_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oak_obj oak_obj_t;

typedef void* (*oak_malloc_fn)(usize size);
typedef void* (*oak_realloc_fn)(void* ptr, usize new_size);
typedef void (*oak_free_fn)(void* ptr);

/* Where an allocation was requested.
 *
 * This is a value, so a helper that allocates on someone else's behalf can
 * take its caller's location and forward it instead of reporting its own. That
 * is the whole point of the type: a macro capturing __FILE__/__LINE__ can only
 * ever name its own expansion site, which is the wrong line for anything one
 * layer down -- and, for memory a script asked for, the wrong file entirely.
 *
 * `file` is borrowed and never copied: it must outlive the allocation, because
 * the tracking allocator only reads it at shutdown. A `__FILE__` literal
 * always does. A forwarded script path must too, so pass a module's interned
 * name rather than a temporary buffer. */
typedef struct oak_source_loc oak_source_loc_t;
struct oak_source_loc
{
  const char* file;
  int line;
};

/* A function rather than a compound literal so that OAK_HERE also works in the
 * C++ translation units this header supports. */
static inline oak_source_loc_t oak_source_loc_make(const char* file, int line)
{
  oak_source_loc_t loc;
  loc.file = file;
  loc.line = line;
  return loc;
}

#define OAK_HERE (oak_source_loc_make(__FILE__, __LINE__))

/* Allocation callbacks must return usable memory: runtime call sites do not
 * null-check. The built-in system and tracking allocators log and abort() on
 * out-of-memory; custom allocators should fail the same way rather than
 * return null. */
typedef struct oak_allocator oak_allocator_t;
struct oak_allocator
{
  void* (*alloc)(oak_allocator_t* self, usize size, oak_source_loc_t at);
  void* (*realloc)(oak_allocator_t* self,
                   void* ptr,
                   usize new_size,
                   oak_source_loc_t at);
  void (*free)(oak_allocator_t* self, void* ptr, oak_source_loc_t at);
  int (*shutdown)(oak_allocator_t* self);
  void* state;
  oak_malloc_fn malloc_fn;
  oak_realloc_fn realloc_fn;
  oak_free_fn free_fn;
};

/* Allocate, reallocate and release with an explicit origin.
 *
 * Pass OAK_HERE when this call site is the origin; pass a location received
 * from a caller when it is not. There is deliberately no macro that hides the
 * argument: a macro can only ever supply its own expansion site, which is the
 * wrong answer for every helper that allocates on a caller's behalf, and
 * hiding the choice is what made those cases easy to get wrong. */
OAK_API void* oak_alloc(oak_allocator_t* a, usize size, oak_source_loc_t at);
OAK_API void* oak_realloc(oak_allocator_t* a,
                          void* ptr,
                          usize new_size,
                          oak_source_loc_t at);
OAK_API void oak_free(oak_allocator_t* a, void* ptr, oak_source_loc_t at);

OAK_API extern oak_allocator_t oak_system_allocator;

/* Initialize an allocator from malloc/realloc/free-compatible functions.
 *
 * Allocation failures are logged and abort, matching the system allocator.
 *
 * All three callbacks must be non-null. */
OAK_API void oak_allocator_init(oak_allocator_t* a,
                                oak_malloc_fn malloc_fn,
                                oak_realloc_fn realloc_fn,
                                oak_free_fn free_fn);
OAK_API void oak_system_allocator_init(oak_allocator_t* a);
OAK_API void oak_tracking_allocator_init(oak_allocator_t* a);

#ifdef __cplusplus
}
#endif
