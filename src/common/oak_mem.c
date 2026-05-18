#include "oak_mem.h"

#include "oak_allocator.h"

static struct oak_allocator_t g_allocator_storage = {
  .alloc = null,
};
static int g_default_initialized;
static struct oak_allocator_t* g_allocator = &g_allocator_storage;

static void oak_mem_ensure_default(void)
{
  if (!g_default_initialized)
  {
    oak_system_allocator_init(&g_allocator_storage);
    g_default_initialized = 1;
  }
}

void* oak_alloc(const usize size, const struct oak_src_loc_t src_loc)
{
  oak_mem_ensure_default();
  return g_allocator->alloc(g_allocator, size, src_loc.file, src_loc.line);
}

void* oak_realloc(void* ptr,
                  const usize size,
                  const struct oak_src_loc_t src_loc)
{
  oak_mem_ensure_default();
  return g_allocator->realloc(g_allocator, ptr, size, src_loc.file,
                             src_loc.line);
}

void oak_free(void* ptr, const struct oak_src_loc_t src_loc)
{
  oak_mem_ensure_default();
  g_allocator->free(g_allocator, ptr, src_loc.file, src_loc.line);
}

void oak_mem_init(void)
{
  oak_tracking_allocator_init(&g_allocator_storage);
  g_allocator = &g_allocator_storage;
  g_default_initialized = 1;
}

void oak_mem_shutdown(void)
{
  g_allocator->shutdown(g_allocator);
  oak_system_allocator_init(&g_allocator_storage);
  g_allocator = &g_allocator_storage;
  g_default_initialized = 1;
}

struct oak_allocator_t* oak_mem_allocator(void)
{
  oak_mem_ensure_default();
  return g_allocator;
}

void oak_mem_set_allocator(struct oak_allocator_t* a)
{
  g_allocator = a;
}
