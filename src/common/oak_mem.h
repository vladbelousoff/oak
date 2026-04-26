#pragma once

#include "oak_list.h"

struct oak_src_loc_t
{
#ifdef OAK_TRACK_MEMORY
  /* C __FILE__ or Oak program path (borrowed); line is __LINE__ or Oak line. */
  const char* file;
  int line;
#else
  int unused;
#endif
};

#ifdef OAK_TRACK_MEMORY
#define OAK_SRC_LOC                                                            \
  (struct oak_src_loc_t)                                                       \
  {                                                                            \
    .file = __FILE__, .line = __LINE__,                                        \
  }
#else
#define OAK_SRC_LOC (struct oak_src_loc_t){ 0 }
#endif

struct oak_mem_header_t
{
  unsigned signature;
  struct oak_list_entry_t link;
  struct oak_src_loc_t src_loc;
  usize size;
};

void* oak_alloc(usize size, struct oak_src_loc_t src_loc);
void* oak_realloc(void* ptr, usize size, struct oak_src_loc_t src_loc);
void oak_free(void* ptr, struct oak_src_loc_t src_loc);

void oak_mem_init();
void oak_mem_shutdown();
