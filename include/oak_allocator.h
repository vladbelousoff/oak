#pragma once

#include "oak_export.h"
#include "oak_types.h"

struct oak_obj_t;

typedef void* (*oak_malloc_fn)(usize size);
typedef void* (*oak_realloc_fn)(void* ptr, usize new_size);
typedef void (*oak_free_fn)(void* ptr);

/* Allocation callbacks must return usable memory: runtime call sites do not
 * null-check. The built-in system and tracking allocators log and abort() on
 * out-of-memory; custom allocators should fail the same way rather than
 * return null. */
struct oak_allocator_t
{
  void* (*alloc)(struct oak_allocator_t* self,
                 usize size,
                 const char* file,
                 int line);
  void* (*realloc)(struct oak_allocator_t* self,
                   void* ptr,
                   usize new_size,
                   const char* file,
                   int line);
  void (*free)(struct oak_allocator_t* self,
               void* ptr,
               const char* file,
               int line);
  int (*shutdown)(struct oak_allocator_t* self);
  void* state;
  oak_malloc_fn malloc_fn;
  oak_realloc_fn realloc_fn;
  oak_free_fn free_fn;
};

#define OAK_ALLOC(a, size) ((a)->alloc((a), (size), __FILE__, __LINE__))
#define OAK_REALLOC(a, ptr, size)                                              \
  ((a)->realloc((a), (ptr), (size), __FILE__, __LINE__))
#define OAK_FREE(a, ptr) ((a)->free((a), (ptr), __FILE__, __LINE__))

OAK_API extern struct oak_allocator_t oak_system_allocator;

/* Initialize an allocator from malloc/realloc/free-compatible functions.
 *
 * Allocation failures are logged and abort, matching the system allocator.
 *
 * All three callbacks must be non-null. */
OAK_API void oak_allocator_init(struct oak_allocator_t* a,
                                oak_malloc_fn malloc_fn,
                                oak_realloc_fn realloc_fn,
                                oak_free_fn free_fn);
OAK_API void oak_system_allocator_init(struct oak_allocator_t* a);
OAK_API void oak_tracking_allocator_init(struct oak_allocator_t* a);
