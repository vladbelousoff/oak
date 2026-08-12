#include "oak_allocator.h"

#include <stdlib.h>
#include <string.h>

#include "oak_list.h"
#include "oak_log.h"
#include "oak_value.h"

/* Runtime call sites do not null-check allocations (see oak_allocator.h), so
 * out-of-memory is fatal for the built-in allocators. */
static void oom_abort(const usize size, const char* file, const int line)
{
  const char* at = file ? oak_path_basename(file) : "?";
  oak_log(OAK_LOG_ERROR,
          "out of memory allocating %lu bytes (%s:%d)",
          (unsigned long)size,
          at,
          line);
  abort();
}


static void* custom_alloc(oak_allocator_t* self,
                          usize size,
                          const char* file,
                          int line)
{
  void* ptr = self->malloc_fn(size);
  if (!ptr && size != 0)
    oom_abort(size, file, line);
  return ptr;
}

static void* custom_realloc(oak_allocator_t* self,
                            void* ptr,
                            usize new_size,
                            const char* file,
                            int line)
{
  void* new_ptr = self->realloc_fn(ptr, new_size);
  if (!new_ptr && new_size != 0)
    oom_abort(new_size, file, line);
  return new_ptr;
}

static void custom_free(oak_allocator_t* self,
                        void* ptr,
                        const char* file,
                        int line)
{
  (void)file;
  (void)line;
  self->free_fn(ptr);
}

static int sys_shutdown(oak_allocator_t* self)
{
  (void)self;
  return 0;
}

oak_allocator_t oak_system_allocator = {
  .alloc = custom_alloc,
  .realloc = custom_realloc,
  .free = custom_free,
  .shutdown = sys_shutdown,
  .state = null,
  .malloc_fn = malloc,
  .realloc_fn = realloc,
  .free_fn = free,
};

void oak_allocator_init(oak_allocator_t* a,
                        oak_malloc_fn malloc_fn,
                        oak_realloc_fn realloc_fn,
                        oak_free_fn free_fn)
{
  a->alloc = custom_alloc;
  a->realloc = custom_realloc;
  a->free = custom_free;
  a->shutdown = sys_shutdown;
  a->state = null;
  a->malloc_fn = malloc_fn;
  a->realloc_fn = realloc_fn;
  a->free_fn = free_fn;
}

void oak_system_allocator_init(oak_allocator_t* a)
{
  *a = oak_system_allocator;
}


#define TRACK_SIG 0xdeadbeef
#define TRACK_SMB 0x77

typedef struct oak_track_header oak_track_header_t;
struct oak_track_header
{
  unsigned signature;
  oak_list_entry_t link;
  const char* file;
  int line;
  usize size;
};

typedef struct oak_tracking_state oak_tracking_state_t;
struct oak_tracking_state
{
  oak_list_entry_t allocations;
};

static inline oak_track_header_t* header_of(void* ptr)
{
  return (oak_track_header_t*)((char*)ptr -
                                     sizeof(oak_track_header_t));
}

static void* track_alloc(oak_allocator_t* self,
                         usize size,
                         const char* file,
                         int line)
{
  oak_tracking_state_t* st = self->state;
  char* data = malloc(sizeof(oak_track_header_t) + size);
  if (!data)
    oom_abort(size, file, line);

  oak_track_header_t* header = (oak_track_header_t*)data;
  header->signature = TRACK_SIG;
  header->file = file;
  header->line = line;
  header->size = size;
  oak_list_add_tail(&st->allocations, &header->link);
  memset(data + sizeof(oak_track_header_t), TRACK_SMB, size);
  return data + sizeof(oak_track_header_t);
}

static void* track_realloc(oak_allocator_t* self,
                           void* ptr,
                           usize new_size,
                           const char* file,
                           int line)
{
  if (!ptr)
    return track_alloc(self, new_size, file, line);

  oak_tracking_state_t* st = self->state;
  oak_track_header_t* old_header = header_of(ptr);
  const usize old_size = old_header->size;

  if (new_size == 0)
  {
    oak_list_remove(&old_header->link);
    free(old_header);
    return null;
  }

  oak_list_remove(&old_header->link);
  char* data = realloc(old_header, sizeof(oak_track_header_t) + new_size);
  if (!data)
    oom_abort(new_size, file, line);

  if (new_size > old_size)
    memset(data + sizeof(oak_track_header_t) + old_size,
           TRACK_SMB,
           new_size - old_size);

  oak_track_header_t* header = (oak_track_header_t*)data;
  header->signature = TRACK_SIG;
  header->file = file;
  header->line = line;
  header->size = new_size;
  oak_list_add_tail(&st->allocations, &header->link);
  return data + sizeof(oak_track_header_t);
}

static void track_free(oak_allocator_t* self,
                       void* ptr,
                       const char* file,
                       int line)
{
  (void)self;
  if (!ptr)
    return;
  oak_track_header_t* header = header_of(ptr);
  if (header->signature != TRACK_SIG)
  {
    const char* at = file ? oak_path_basename(file) : "?";
    oak_log(OAK_LOG_ERROR, "memory signature mismatch: %s:%d", at, line);
  }
  else
  {
    oak_list_remove(&header->link);
    /* Invalidate the signature so a double-free is reported as a mismatch
     * instead of corrupting the heap. */
    header->signature = 0;
    free(header);
  }
}

static int track_shutdown(oak_allocator_t* self)
{
  oak_tracking_state_t* st = self->state;
  int leak_count = 0;
  oak_list_entry_t* entry;
  oak_list_entry_t* safe;
  oak_list_for_each_safe(entry, safe, &st->allocations)
  {
    oak_track_header_t* header =
        oak_container_of(entry, oak_track_header_t, link);
    const char* at = header->file ? oak_path_basename(header->file) : "?";
    oak_log(OAK_LOG_ERROR,
            "leaked memory: %s:%d, size: %lu",
            at,
            header->line,
            (unsigned long)header->size);
    oak_list_remove(&header->link);
    free(header);
    ++leak_count;
  }
  free(st);
  self->state = null;
  return leak_count;
}

void oak_tracking_allocator_init(oak_allocator_t* a)
{
  oak_tracking_state_t* st = malloc(sizeof(oak_tracking_state_t));
  if (!st)
  {
    oak_system_allocator_init(a);
    return;
  }
  oak_list_init(&st->allocations);
  a->alloc = track_alloc;
  a->realloc = track_realloc;
  a->free = track_free;
  a->shutdown = track_shutdown;
  a->state = st;
  a->malloc_fn = null;
  a->realloc_fn = null;
  a->free_fn = null;
}
