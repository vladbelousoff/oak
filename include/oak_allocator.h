#pragma once

#include "oak_export.h"
#include "oak_types.h"

struct oak_obj_t;

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
  int collecting_cycles;
};

#define OAK_ALLOC(a, size) ((a)->alloc((a), (size), __FILE__, __LINE__))
#define OAK_REALLOC(a, ptr, size)                                              \
  ((a)->realloc((a), (ptr), (size), __FILE__, __LINE__))
#define OAK_FREE(a, ptr) ((a)->free((a), (ptr), __FILE__, __LINE__))

OAK_API extern struct oak_allocator_t oak_system_allocator;

OAK_API void oak_system_allocator_init(struct oak_allocator_t* a);
OAK_API void oak_tracking_allocator_init(struct oak_allocator_t* a);
