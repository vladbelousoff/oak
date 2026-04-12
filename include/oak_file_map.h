#pragma once

#include <stddef.h>

struct oak_file_map_t
{
  char* data;
  size_t size;
#if defined(_WIN32)
  void* mapping_handle;
#else
  size_t map_length;
#endif
};

int oak_file_map(const char* path, struct oak_file_map_t* out);
void oak_file_unmap(struct oak_file_map_t* map);
