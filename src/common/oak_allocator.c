#include "oak_allocator.h"

#include <stdlib.h>
#include <string.h>

#include "oak_list.h"
#include "oak_log.h"
#include "oak_value.h"

/* --- System allocator (thin malloc wrapper, no tracking) --- */

static void* sys_alloc(struct oak_allocator_t* self,
                       usize size,
                       const char* file,
                       int line)
{
  (void)self;
  (void)file;
  (void)line;
  return malloc(size);
}

static void* sys_realloc(struct oak_allocator_t* self,
                         void* ptr,
                         usize new_size,
                         const char* file,
                         int line)
{
  (void)self;
  (void)file;
  (void)line;
  return realloc(ptr, new_size);
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
  oak_collect_cycles(self);
  return 0;
}

struct oak_allocator_t oak_system_allocator = {
  .alloc = sys_alloc,
  .realloc = sys_realloc,
  .free = sys_free,
  .shutdown = sys_shutdown,
  .state = null,
  .cycle_objects = null,
  .cycle_decrefs = 0,
  .collecting_cycles = 0,
};

void oak_system_allocator_init(struct oak_allocator_t* a)
{
  *a = oak_system_allocator;
}

/* --- Tracking allocator (leak detection) --- */

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
    return null;

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
  {
    oak_list_add_tail(&st->allocations, &old_header->link);
    return null;
  }

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
    free(header);
  }
}

static int track_shutdown(struct oak_allocator_t* self)
{
  oak_collect_cycles(self);
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
  a->cycle_objects = null;
  a->cycle_decrefs = 0;
  a->collecting_cycles = 0;
}
