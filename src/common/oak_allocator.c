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


static void* sys_alloc(struct oak_allocator_t* self,
                       usize size,
                       const char* file,
                       int line)
{
  (void)self;
  void* ptr = malloc(size);
  if (!ptr && size != 0)
    oom_abort(size, file, line);
  return ptr;
}

static void* sys_realloc(struct oak_allocator_t* self,
                         void* ptr,
                         usize new_size,
                         const char* file,
                         int line)
{
  (void)self;
  void* new_ptr = realloc(ptr, new_size);
  if (!new_ptr && new_size != 0)
    oom_abort(new_size, file, line);
  return new_ptr;
}

static void sys_free(struct oak_allocator_t* self,
                     void* ptr,
                     const char* file,
                     int line)
{
  (void)self;
  (void)file;
  (void)line;
  free(ptr);
}

static int sys_shutdown(struct oak_allocator_t* self)
{
  (void)self;
  return 0;
}

struct oak_allocator_t oak_system_allocator = {
  .alloc = sys_alloc,
  .realloc = sys_realloc,
  .free = sys_free,
  .shutdown = sys_shutdown,
  .state = null,
};

void oak_system_allocator_init(struct oak_allocator_t* a)
{
  *a = oak_system_allocator;
}


#define TRACK_SIG 0xdeadbeef
#define TRACK_SMB 0x77

struct oak_track_header_t
{
  unsigned signature;
  struct oak_list_entry_t link;
  const char* file;
  int line;
  usize size;
};

struct oak_tracking_state_t
{
  struct oak_list_entry_t allocations;
};

static inline struct oak_track_header_t* header_of(void* ptr)
{
  return (struct oak_track_header_t*)((char*)ptr -
                                     sizeof(struct oak_track_header_t));
}

static void* track_alloc(struct oak_allocator_t* self,
                         usize size,
                         const char* file,
                         int line)
{
  struct oak_tracking_state_t* st = self->state;
  char* data = malloc(sizeof(struct oak_track_header_t) + size);
  if (!data)
    oom_abort(size, file, line);

  struct oak_track_header_t* header = (struct oak_track_header_t*)data;
  header->signature = TRACK_SIG;
  header->file = file;
  header->line = line;
  header->size = size;
  oak_list_add_tail(&st->allocations, &header->link);
  memset(data + sizeof(struct oak_track_header_t), TRACK_SMB, size);
  return data + sizeof(struct oak_track_header_t);
}

static void* track_realloc(struct oak_allocator_t* self,
                           void* ptr,
                           usize new_size,
                           const char* file,
                           int line)
{
  if (!ptr)
    return track_alloc(self, new_size, file, line);

  struct oak_tracking_state_t* st = self->state;
  struct oak_track_header_t* old_header = header_of(ptr);
  const usize old_size = old_header->size;

  if (new_size == 0)
  {
    oak_list_remove(&old_header->link);
    free(old_header);
    return null;
  }

  oak_list_remove(&old_header->link);
  char* data = realloc(old_header, sizeof(struct oak_track_header_t) + new_size);
  if (!data)
    oom_abort(new_size, file, line);

  if (new_size > old_size)
    memset(data + sizeof(struct oak_track_header_t) + old_size,
           TRACK_SMB,
           new_size - old_size);

  struct oak_track_header_t* header = (struct oak_track_header_t*)data;
  header->signature = TRACK_SIG;
  header->file = file;
  header->line = line;
  header->size = new_size;
  oak_list_add_tail(&st->allocations, &header->link);
  return data + sizeof(struct oak_track_header_t);
}

static void track_free(struct oak_allocator_t* self,
                       void* ptr,
                       const char* file,
                       int line)
{
  (void)self;
  if (!ptr)
    return;
  struct oak_track_header_t* header = header_of(ptr);
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

static int track_shutdown(struct oak_allocator_t* self)
{
  struct oak_tracking_state_t* st = self->state;
  int leak_count = 0;
  struct oak_list_entry_t* entry;
  struct oak_list_entry_t* safe;
  oak_list_for_each_safe(entry, safe, &st->allocations)
  {
    struct oak_track_header_t* header =
        oak_container_of(entry, struct oak_track_header_t, link);
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

void oak_tracking_allocator_init(struct oak_allocator_t* a)
{
  struct oak_tracking_state_t* st = malloc(sizeof(struct oak_tracking_state_t));
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
}
