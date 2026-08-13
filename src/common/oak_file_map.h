#pragma once

#include "oak_export.h"
#include "oak_types.h"

typedef struct oak_file_map oak_file_map_t;
struct oak_file_map
{
  char* data;
  usize size;
#if defined(_WIN32)
  void* mapping_handle;
#else
  usize map_length;
#endif
};

int oak_file_map(const char* path, oak_file_map_t* out);
void oak_file_unmap(oak_file_map_t* map);
