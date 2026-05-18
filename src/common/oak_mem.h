#pragma once

#include "oak_allocator.h"
#include "oak_export.h"

struct oak_src_loc_t
{
  const char* file;
  int line;
};

#define OAK_SRC_LOC                                                            \
  (struct oak_src_loc_t)                                                       \
  {                                                                            \
    .file = __FILE__, .line = __LINE__,                                        \
  }

OAK_API void* oak_alloc(usize size, struct oak_src_loc_t src_loc);
OAK_API void* oak_realloc(void* ptr, usize size, struct oak_src_loc_t src_loc);
OAK_API void oak_free(void* ptr, struct oak_src_loc_t src_loc);

OAK_API void oak_mem_init(void);
OAK_API void oak_mem_shutdown(void);
OAK_API struct oak_allocator_t* oak_mem_allocator(void);
OAK_API void oak_mem_set_allocator(struct oak_allocator_t* a);
