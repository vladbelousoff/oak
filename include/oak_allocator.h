#pragma once

#include "oak_export.h"
#include "oak_types.h"

struct oak_obj_t;

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
  struct oak_obj_t* cycle_objects;
  int cycle_decrefs;
  /* Retained-decref count that arms the next automatic cycle collection.
   * Updated by oak_collect_cycles to the cost of the last scan so collection
   * work stays amortized O(1) per decref; 0 means "use the default floor". */
  int cycle_trigger;
  int collecting_cycles;
};

#define OAK_ALLOC(a, size) ((a)->alloc((a), (size), __FILE__, __LINE__))
#define OAK_REALLOC(a, ptr, size)                                              \
  ((a)->realloc((a), (ptr), (size), __FILE__, __LINE__))
#define OAK_FREE(a, ptr) ((a)->free((a), (ptr), __FILE__, __LINE__))

OAK_API extern struct oak_allocator_t oak_system_allocator;

OAK_API void oak_system_allocator_init(struct oak_allocator_t* a);
OAK_API void oak_tracking_allocator_init(struct oak_allocator_t* a);
