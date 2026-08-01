#include "oak_allocator.h"
#include "oak_dynarr.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define CHECK(expr)                                                            \
  do                                                                           \
  {                                                                            \
    if (!(expr))                                                               \
      return __LINE__;                                                         \
  } while (0)

struct fail_state_t
{
  int fail_alloc;
  int fail_realloc;
};

static int custom_alloc_count;
static int custom_realloc_count;
static int custom_free_count;

static void* counted_malloc(usize size)
{
  ++custom_alloc_count;
  return malloc(size);
}

static void* counted_realloc(void* ptr, usize size)
{
  ++custom_realloc_count;
  return realloc(ptr, size);
}

static void counted_free(void* ptr)
{
  ++custom_free_count;
  free(ptr);
}

static void* fail_alloc(struct oak_allocator_t* self,
                        usize size,
                        const char* file,
                        int line)
{
  (void)file;
  (void)line;
  struct fail_state_t* state = self->state;
  return state->fail_alloc ? null : malloc(size);
}

static void* fail_realloc(struct oak_allocator_t* self,
                          void* ptr,
                          usize size,
                          const char* file,
                          int line)
{
  (void)file;
  (void)line;
  struct fail_state_t* state = self->state;
  return state->fail_realloc ? null : realloc(ptr, size);
}

static void fail_free(struct oak_allocator_t* self,
                      void* ptr,
                      const char* file,
                      int line)
{
  (void)self;
  (void)file;
  (void)line;
  free(ptr);
}

static int fail_shutdown(struct oak_allocator_t* self)
{
  (void)self;
  return 0;
}

static struct oak_allocator_t fail_allocator(struct fail_state_t* state)
{
  struct oak_allocator_t allocator = {
    .alloc = fail_alloc,
    .realloc = fail_realloc,
    .free = fail_free,
    .shutdown = fail_shutdown,
    .state = state,
  };
  return allocator;
}

int main(void)
{
  struct oak_allocator_t custom;
  oak_allocator_init(
      &custom, counted_malloc, counted_realloc, counted_free);
  void* custom_ptr = OAK_ALLOC(&custom, 8);
  CHECK(custom_ptr != null);
  custom_ptr = OAK_REALLOC(&custom, custom_ptr, 16);
  CHECK(custom_ptr != null);
  OAK_FREE(&custom, custom_ptr);
  CHECK(custom_alloc_count == 1);
  CHECK(custom_realloc_count == 1);
  CHECK(custom_free_count == 1);
  CHECK(custom.shutdown(&custom) == 0);

  struct oak_allocator_t tracking;
  oak_tracking_allocator_init(&tracking);

  int* items = null;
  CHECK(oak_dynarr_init(&tracking, &items, sizeof *items));
  CHECK(items != null);
  CHECK(oak_dynarr_count(items) == 0);
  CHECK(oak_dynarr_capacity(items) == 0);

  for (int i = 0; i < 40; ++i)
    CHECK(oak_dynarr_push(&items, &i));
  CHECK(oak_dynarr_count(items) == 40);
  for (int i = 0; i < 40; ++i)
    CHECK(items[i] == i);

  CHECK(oak_dynarr_reserve(&items, 100));
  CHECK(oak_dynarr_capacity(items) >= 100);
  CHECK(oak_dynarr_resize(&items, 105));
  for (int i = 40; i < 105; ++i)
    CHECK(items[i] == 0);

  int popped = -1;
  CHECK(oak_dynarr_pop(&items, &popped));
  CHECK(popped == 0);
  CHECK(oak_dynarr_resize(&items, 0));
  CHECK(!oak_dynarr_pop(&items, null));
  oak_dynarr_free(&items);
  CHECK(items == null);

  union { void* _p; double _d; long long _ll; long double _ld; } *aligned = null;
  CHECK(oak_dynarr_init(&tracking, &aligned, sizeof *aligned));
  CHECK((uintptr_t)aligned % sizeof *aligned == 0);
  oak_dynarr_free(&aligned);
  CHECK(tracking.shutdown(&tracking) == 0);

  struct fail_state_t state = { .fail_alloc = 1 };
  struct oak_allocator_t failing = fail_allocator(&state);
  int* original = (int*)(uintptr_t)1;
  CHECK(!oak_dynarr_init(&failing, &original, sizeof *original));
  CHECK(original == (int*)(uintptr_t)1);

  state.fail_alloc = 0;
  void* oversized = null;
  CHECK(oak_dynarr_init(&failing, &oversized, SIZE_MAX));
  CHECK(!oak_dynarr_reserve(&oversized, 1));
  oak_dynarr_free(&oversized);

  int* preserved = null;
  CHECK(oak_dynarr_init(&failing, &preserved, sizeof *preserved));
  for (int i = 0; i < 8; ++i)
    CHECK(oak_dynarr_push(&preserved, &i));
  int* before = preserved;
  state.fail_realloc = 1;
  int ninth = 9;
  CHECK(!oak_dynarr_push(&preserved, &ninth));
  CHECK(preserved == before);
  CHECK(oak_dynarr_count(preserved) == 8);
  for (int i = 0; i < 8; ++i)
    CHECK(preserved[i] == i);
  CHECK(!oak_dynarr_reserve(&preserved, -1));
  CHECK(!oak_dynarr_resize(&preserved, -1));
  oak_dynarr_free(&preserved);

  return 0;
}
