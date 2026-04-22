#include "oak_mem.h"

#include <stdlib.h>
#include <string.h>

#ifdef OAK_TRACK_MEMORY
#include "oak_list.h"
#include "oak_log.h"

static struct oak_list_entry_t memory_allocations;
static int memory_tracking_enabled = 0;

static inline struct oak_mem_header_t* header_of(void* ptr)
{
  return (struct oak_mem_header_t*)((char*)ptr - sizeof(struct oak_mem_header_t));
}
#endif

#define OAK_MEM_SMB 0x77
#define OAK_MEM_SIG 0xdeadbeef

void* oak_alloc(const usize size, const struct oak_src_loc_t src_loc)
{
#ifdef OAK_TRACK_MEMORY
  oak_assert(memory_tracking_enabled);
  char* data = malloc(sizeof(struct oak_mem_header_t) + size);
  if (data == null)
    return null;

  struct oak_mem_header_t* header = (struct oak_mem_header_t*)data;
  header->signature = OAK_MEM_SIG;
  header->src_loc = src_loc;
  header->size = size;
  oak_list_add_tail(&memory_allocations, &header->link);
  memset(data + sizeof(struct oak_mem_header_t), OAK_MEM_SMB, size);
  return data + sizeof(struct oak_mem_header_t);
#else
  (void)src_loc;
  return malloc(size);
#endif
}

void* oak_realloc(void* ptr,
                  const usize size,
                  const struct oak_src_loc_t src_loc)
{
#ifdef OAK_TRACK_MEMORY
  oak_assert(memory_tracking_enabled);
  if (ptr == null)
    return oak_alloc(size, src_loc);

  struct oak_mem_header_t* old_header = header_of(ptr);
  oak_list_remove(&old_header->link);

  const usize old_size = old_header->size;
  char* data = realloc(old_header, sizeof(struct oak_mem_header_t) + size);
  if (data == null)
    return null;

  if (size > old_size)
    memset(data + sizeof(struct oak_mem_header_t) + old_size, OAK_MEM_SMB, size - old_size);

  struct oak_mem_header_t* header = (struct oak_mem_header_t*)data;
  header->signature = OAK_MEM_SIG;
  header->src_loc = src_loc;
  header->size = size;
  oak_list_add_tail(&memory_allocations, &header->link);
  return data + sizeof(struct oak_mem_header_t);
#else
  (void)src_loc;
  return realloc(ptr, size);
#endif
}

void oak_free(void* ptr, const struct oak_src_loc_t src_loc)
{
#ifdef OAK_TRACK_MEMORY
  oak_assert(memory_tracking_enabled);
  struct oak_mem_header_t* header = header_of(ptr);
  if (header->signature != OAK_MEM_SIG)
  {
    oak_log(OAK_LOG_ERROR,
            "memory signature mismatch: %s:%lu",
            oak_path_basename(src_loc.file),
            src_loc.line);
  }
  else
  {
    oak_list_remove(&header->link);
    free(header);
  }
#else
  free(ptr);
#endif
}

void oak_mem_init()
{
#ifdef OAK_TRACK_MEMORY
  oak_list_init(&memory_allocations);
  memory_tracking_enabled = 1;
#endif
}

void oak_mem_shutdown()
{
#ifdef OAK_TRACK_MEMORY
  oak_assert(memory_tracking_enabled);
  struct oak_list_entry_t* entry;
  struct oak_list_entry_t* safe;
  oak_list_for_each_safe(entry, safe, &memory_allocations)
  {
    struct oak_mem_header_t* header =
        oak_container_of(entry, struct oak_mem_header_t, link);
    oak_log(OAK_LOG_ERROR,
            "leaked memory: %s:%lu, size: %lu",
            oak_path_basename(header->src_loc.file),
            header->src_loc.line,
            header->size);
    oak_list_remove(&header->link);
    free(header);
  }
  memory_tracking_enabled = 0;
#endif
}

#undef OAK_MEM_SMB
#undef OAK_MEM_SIG
