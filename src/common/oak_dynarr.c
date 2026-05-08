#include "oak_dynarr.h"
#include "oak_mem.h"
#include "oak_types.h"

#include <string.h>

/* All functions access the `items` field via memcpy on its address to avoid
 * strict-aliasing undefined behaviour: the caller passes &arr.items (a T**)
 * as void*; we read/write the pointer-sized value at that address with memcpy
 * rather than through a void** dereference. */

void oak_dynarr_init(void* items_field_ptr, int* count, int* capacity)
{
  void* null_ptr = null;
  memcpy(items_field_ptr, &null_ptr, sizeof(void*));
  *count = 0;
  *capacity = 0;
}

void oak_dynarr_push(void* items_field_ptr,
                     int* count,
                     int* capacity,
                     const void* item,
                     int item_size)
{
  void* items;
  memcpy(&items, items_field_ptr, sizeof(void*));

  if (*count >= *capacity)
  {
    const int nc = *capacity < 8 ? 8 : *capacity * 2;
    items = oak_realloc(items, (usize)nc * (usize)item_size, OAK_SRC_LOC);
    *capacity = nc;
  }

  memcpy((char*)items + (usize)(*count) * (usize)item_size,
         item,
         (usize)item_size);
  (*count)++;

  memcpy(items_field_ptr, &items, sizeof(void*));
}

void oak_dynarr_free(void* items_field_ptr, int* count, int* capacity)
{
  void* items;
  memcpy(&items, items_field_ptr, sizeof(void*));
  if (items)
    oak_free(items, OAK_SRC_LOC);
  items = null;
  memcpy(items_field_ptr, &items, sizeof(void*));
  *count = 0;
  *capacity = 0;
}
