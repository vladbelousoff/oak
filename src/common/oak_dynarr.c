#include "oak_dynarr.h"
#include "oak_allocator.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct oak_dynarr_header_t
{
  max_align_t alignment;
  struct oak_allocator_t* allocator;
  usize item_size;
  int count;
  int capacity;
};

_Static_assert(sizeof(struct oak_dynarr_header_t) % _Alignof(max_align_t) == 0,
               "dynamic array data must be maximally aligned");

static struct oak_dynarr_header_t* header_of(const void* items)
{
  return (struct oak_dynarr_header_t*)((char*)items -
                                      sizeof(struct oak_dynarr_header_t));
}

static void load_ref(const void* array_ref, void** items)
{
  memcpy(items, array_ref, sizeof(*items));
}

static void store_ref(void* array_ref, void* items)
{
  memcpy(array_ref, &items, sizeof(items));
}

static int allocation_size(const usize item_size,
                           const int capacity,
                           usize* out)
{
  if (capacity < 0 || item_size == 0 ||
      (usize)capacity >
          (SIZE_MAX - sizeof(struct oak_dynarr_header_t)) / item_size)
    return 0;
  *out = sizeof(struct oak_dynarr_header_t) + (usize)capacity * item_size;
  return 1;
}

int oak_dynarr_init(struct oak_allocator_t* allocator,
                    void* array_ref,
                    const usize item_size)
{
  if (!allocator || !array_ref || item_size == 0)
    return 0;
  struct oak_dynarr_header_t* h = OAK_ALLOC(allocator, sizeof(*h));
  if (!h)
    return 0;
  h->allocator = allocator;
  h->item_size = item_size;
  h->count = 0;
  h->capacity = 0;
  store_ref(array_ref, h + 1);
  return 1;
}

int oak_dynarr_reserve(void* array_ref, int capacity)
{
  void* items;
  if (!array_ref)
    return 0;
  load_ref(array_ref, &items);
  if (!items || capacity < 0)
    return 0;
  struct oak_dynarr_header_t* h = header_of(items);
  if (capacity <= h->capacity)
    return 1;
  usize size;
  if (!allocation_size(h->item_size, capacity, &size))
    return 0;
  struct oak_dynarr_header_t* grown = OAK_REALLOC(h->allocator, h, size);
  if (!grown)
    return 0;
  grown->capacity = capacity;
  store_ref(array_ref, grown + 1);
  return 1;
}

int oak_dynarr_push(void* array_ref, const void* item)
{
  void* items;
  if (!array_ref || !item)
    return 0;
  load_ref(array_ref, &items);
  if (!items)
    return 0;
  struct oak_dynarr_header_t* h = header_of(items);
  if (h->count == INT_MAX)
    return 0;
  if (h->count == h->capacity)
  {
    int capacity = h->capacity < 8 ? 8 : h->capacity;
    if (capacity == h->capacity)
      capacity = capacity > INT_MAX / 2 ? INT_MAX : capacity * 2;
    if (!oak_dynarr_reserve(array_ref, capacity))
      return 0;
    load_ref(array_ref, &items);
    h = header_of(items);
  }
  memcpy((char*)items + (usize)h->count * h->item_size,
         item,
         h->item_size);
  ++h->count;
  return 1;
}

int oak_dynarr_resize(void* array_ref, int count)
{
  void* items;
  if (!array_ref || count < 0)
    return 0;
  load_ref(array_ref, &items);
  if (!items)
    return 0;
  struct oak_dynarr_header_t* h = header_of(items);
  const int old_count = h->count;
  if (count > h->capacity && !oak_dynarr_reserve(array_ref, count))
    return 0;
  load_ref(array_ref, &items);
  h = header_of(items);
  if (count > old_count)
    memset((char*)items + (usize)old_count * h->item_size, 0,
           (usize)(count - old_count) * h->item_size);
  h->count = count;
  return 1;
}

int oak_dynarr_pop(void* array_ref, void* out_item)
{
  void* items;
  if (!array_ref)
    return 0;
  load_ref(array_ref, &items);
  if (!items)
    return 0;
  struct oak_dynarr_header_t* h = header_of(items);
  if (h->count == 0)
    return 0;
  --h->count;
  if (out_item)
    memcpy(out_item,
           (char*)items + (usize)h->count * h->item_size,
           h->item_size);
  return 1;
}

int oak_dynarr_count(const void* items)
{
  return items ? header_of(items)->count : 0;
}

int oak_dynarr_capacity(const void* items)
{
  return items ? header_of(items)->capacity : 0;
}

void oak_dynarr_free(void* array_ref)
{
  void* items;
  if (!array_ref)
    return;
  load_ref(array_ref, &items);
  if (!items)
    return;
  struct oak_dynarr_header_t* h = header_of(items);
  OAK_FREE(h->allocator, h);
  store_ref(array_ref, null);
}
