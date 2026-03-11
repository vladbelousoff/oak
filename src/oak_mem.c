#include "oak_mem.h"

#include <stdlib.h>
#include <string.h>

#ifdef OAK_TRACK_MEMORY
#include "oak_list.h"
#include "oak_log.h"
#include <assert.h>
#endif

#ifdef OAK_TRACK_MEMORY
#define OAK_MEM_SMB 0x77
#define OAK_MEM_SIG 0xdeadbeef
static oak_list_head_t mem_allocs;
static int mem_inited = 0;
#endif

void* oak_mem_acquire(const oak_src_loc_t src_loc, const size_t size)
{
#ifdef OAK_TRACK_MEMORY
  assert(mem_inited);
  // It's not a memory leak, it's just a trick to add a bit more
  // info about allocated memory, so...
  // ReSharper disable once CppDFAMemoryLeak
  char* data = malloc(sizeof(oak_mem_header_t) + size);
  if (data == NULL)
  {
    return NULL;
  }

  oak_mem_header_t* header = (oak_mem_header_t*)data;
  header->signature = OAK_MEM_SIG;
  header->src_loc = src_loc;
  header->size = size;
  oak_list_add_tail(&mem_allocs, &header->link);
  // Mark the memory with 0x77 to be able to debug uninitialized memory
  memset(&data[sizeof(oak_mem_header_t)], OAK_MEM_SMB, size);
  // Return only the necessary piece and hide the header
  // ReSharper disable once CppDFAMemoryLeak
  return &data[sizeof(oak_mem_header_t)];
#else
  (void)src_loc;
  return malloc(size);
#endif
}

void* oak_mem_realloc(const oak_src_loc_t src_loc, void* ptr, const size_t size)
{
#ifdef OAK_TRACK_MEMORY
  assert(mem_inited);
  if (ptr == NULL)
  {
    return oak_mem_acquire(src_loc, size);
  }

  // Find the header with meta-information
  oak_mem_header_t* old_header =
      (oak_mem_header_t*)((char*)ptr - sizeof(oak_mem_header_t));

  // Remove the entry in case the address changed
  oak_list_remove(&old_header->link);

  // Reallocate the entire block including the header
  const size_t old_size = old_header->size;
  // ReSharper disable once CppDFAMemoryLeak
  char* data = realloc(old_header, sizeof(oak_mem_header_t) + size);
  if (data == NULL)
  {
    return NULL;
  }

  // Initialize newly allocated memory with 0x77 if size increased
  if (size > old_size)
  {
    memset(&data[sizeof(oak_mem_header_t) + old_size],
           OAK_MEM_SMB,
           size - old_size);
  }

  oak_mem_header_t* header = (oak_mem_header_t*)data;
  oak_list_add_tail(&mem_allocs, &header->link);

  // Update header information
  header->signature = OAK_MEM_SIG;
  header->src_loc = src_loc;
  header->size = size;

  // Return only the necessary piece and hide the header
  // ReSharper disable once CppDFAMemoryLeak
  return &data[sizeof(oak_mem_header_t)];
#else
  (void)src_loc;
  return realloc(ptr, size);
#endif
}

void oak_mem_release(const oak_src_loc_t src_loc, void* ptr)
{
#ifdef OAK_TRACK_MEMORY
  assert(mem_inited);
  // Find the header with meta-information
  oak_mem_header_t* header =
      (oak_mem_header_t*)((char*)ptr - sizeof(oak_mem_header_t));
  if (header->signature != OAK_MEM_SIG)
  {
    oak_log(OAK_LOG_ERR,
            "Memory signature mismatch: %s:%lu",
            oak_filename(src_loc.file),
            src_loc.line);
  }
  else
  {
    oak_list_remove(&header->link);
    // Now we can free the real allocated piece
    free(header);
  }
#else
  free(ptr);
#endif
}

void oak_mem_init()
{
#ifdef OAK_TRACK_MEMORY
  oak_list_init(&mem_allocs);
  mem_inited = 1;
#endif
}

void oak_mem_shutdown()
{
#ifdef OAK_TRACK_MEMORY
  assert(mem_inited);
  oak_list_entry_t* entry;
  oak_list_entry_t* safe;
  oak_list_for_each_safe(entry, safe, &mem_allocs)
  {
    oak_mem_header_t* header = oak_container_of(entry, oak_mem_header_t, link);
    oak_log(OAK_LOG_ERR,
            "Leaked memory: %s:%lu, size: %lu",
            oak_filename(header->src_loc.file),
            header->src_loc.line,
            header->size);
    oak_list_remove(&header->link);
    free(header);
  }
  mem_inited = 0;
#endif
}

#ifdef OAK_TRACK_MEMORY
#undef OAK_MEM_SMB
#undef OAK_MEM_SIG
#endif
