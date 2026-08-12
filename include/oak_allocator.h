#pragma once

#include "oak_export.h"
#include "oak_types.h"

typedef struct oak_obj oak_obj_t;

typedef void* (*oak_malloc_fn)(usize size);
typedef void* (*oak_realloc_fn)(void* ptr, usize new_size);
typedef void (*oak_free_fn)(void* ptr);

/* Allocation callbacks must return usable memory: runtime call sites do not
 * null-check. The built-in system and tracking allocators log and abort() on
 * out-of-memory; custom allocators should fail the same way rather than
 * return null. */
typedef struct oak_allocator oak_allocator_t;
struct oak_allocator
{
  void* (*alloc)(oak_allocator_t* self,
                 usize size,
                 const char* file,
                 int line);
  void* (*realloc)(oak_allocator_t* self,
                   void* ptr,
                   usize new_size,
                   const char* file,
                   int line);
  void (*free)(oak_allocator_t* self,
               void* ptr,
               const char* file,
               int line);
  int (*shutdown)(oak_allocator_t* self);
  void* state;
  oak_malloc_fn malloc_fn;
  oak_realloc_fn realloc_fn;
  oak_free_fn free_fn;
};

#define OAK_ALLOC(a, size) ((a)->alloc((a), (size), __FILE__, __LINE__))
#define OAK_REALLOC(a, ptr, size)                                              \
  ((a)->realloc((a), (ptr), (size), __FILE__, __LINE__))
#define OAK_FREE(a, ptr) ((a)->free((a), (ptr), __FILE__, __LINE__))

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
