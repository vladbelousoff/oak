#pragma once

#include "oak_list.h"

typedef struct
{
#ifdef OAK_TRACK_MEMORY
  const char* file;
  int line;
#else
  int unused;
#endif
} oak_src_loc_t;

#ifdef OAK_TRACK_MEMORY
#define OAK_SRC_LOC                                                            \
  (oak_src_loc_t)                                                              \
  {                                                                            \
    .file = __FILE__, .line = __LINE__,                                        \
  }
#else
#define OAK_SRC_LOC (oak_src_loc_t){ 0 }
#endif

typedef struct
{
  unsigned signature;
  oak_list_entry_t link;
  oak_src_loc_t src_loc;
  size_t size;
} oak_mem_header_t;

void* oak_alloc(oak_src_loc_t src_loc, size_t size);
void* oak_realloc(oak_src_loc_t src_loc, void* ptr, size_t size);
void oak_free(oak_src_loc_t src_loc, void* ptr);

void oak_mem_init();
void oak_mem_shutdown();
