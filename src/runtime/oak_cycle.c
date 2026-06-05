#include "internal/oak_cycle.h"

#include "oak_allocator.h"
#include "oak_atomic.h"

/* See internal/oak_cycle.h for the collector model and invariants. */

struct oak_cycle_entry_t
{
  struct oak_obj_t* obj;
  int external_refs;
  int reachable;
};

typedef void (*oak_cycle_edge_fn)(struct oak_obj_t* child, void* ctx);

/* Visit every owning edge of `obj` that points at another cycle-capable
 * object. Weak references are not stored in these payloads, so they are
 * naturally excluded. The trait vtable is visited so its liveness propagates,
 * but it is itself externally rooted (chunk constant) and never collected. */
static void oak_obj_visit_cycle_edges(struct oak_obj_t* obj,
                                      oak_cycle_edge_fn visit,
                                      void* ctx)
{
#define VISIT_VALUE(value)                                                     \
  do                                                                           \
  {                                                                            \
    const struct oak_value_t _v = (value);                                     \
    if (_v.tag == OAK_TAG_OBJ && oak_obj_is_cycle_capable(_v.as.obj))          \
      visit(_v.as.obj, ctx);                                                    \
  } while (0)

  if (obj->type == OAK_OBJ_ARRAY)
  {
    struct oak_obj_array_t* arr = (struct oak_obj_array_t*)obj;
    for (usize i = 0; i < arr->length; ++i)
      VISIT_VALUE(arr->items[i]);
  }
  else if (obj->type == OAK_OBJ_MAP)
  {
    struct oak_obj_map_t* map = (struct oak_obj_map_t*)obj;
    for (usize i = 0; i < map->length; ++i)
    {
      VISIT_VALUE(map->entries[i].key);
      VISIT_VALUE(map->entries[i].value);
    }
  }
  else if (obj->type == OAK_OBJ_RECORD)
  {
    struct oak_obj_record_t* record = (struct oak_obj_record_t*)obj;
    for (int i = 0; i < record->field_count; ++i)
      VISIT_VALUE(record->fields[i]);
  }
  else if (obj->type == OAK_OBJ_TRAIT_OBJECT)
  {
    struct oak_obj_trait_object_t* trait = (struct oak_obj_trait_object_t*)obj;
    VISIT_VALUE(trait->value);
    visit((struct oak_obj_t*)trait->vtable, ctx);
  }

#undef VISIT_VALUE
}

struct oak_cycle_scan_ctx_t
{
  struct oak_allocator_t* allocator;
  struct oak_cycle_entry_t* entries;
};

static void oak_cycle_subtract_internal_ref(struct oak_obj_t* child, void* ctx)
{
  struct oak_cycle_scan_ctx_t* scan = ctx;
  if (child->allocator == scan->allocator &&
      (child->cycle_flags & OAK_CYCLE_REGISTERED))
    scan->entries[child->cycle_index].external_refs--;
}

struct oak_cycle_mark_ctx_t
{
  struct oak_allocator_t* allocator;
  struct oak_cycle_entry_t* entries;
  usize* queue;
  usize* tail;
};

static void oak_cycle_mark_child(struct oak_obj_t* child, void* ctx)
{
  struct oak_cycle_mark_ctx_t* mark = ctx;
  if (child->allocator != mark->allocator ||
      !(child->cycle_flags & OAK_CYCLE_REGISTERED))
    return;
  struct oak_cycle_entry_t* entry = &mark->entries[child->cycle_index];
  if (entry->reachable)
    return;
  entry->reachable = 1;
  mark->queue[(*mark->tail)++] = child->cycle_index;
}

usize oak_collect_cycles(struct oak_allocator_t* allocator)
{
  if (!allocator || allocator->collecting_cycles || !allocator->cycle_objects)
    return 0;

  usize count = 0;
  for (struct oak_obj_t* obj = allocator->cycle_objects; obj;
       obj = obj->cycle_next)
    ++count;

  /* Scratch goes through the Oak allocator so it participates in leak
   * tracking. Both buffers are released before returning. */
  struct oak_cycle_entry_t* entries =
      OAK_ALLOC(allocator, count * sizeof(struct oak_cycle_entry_t));
  usize* queue = OAK_ALLOC(allocator, count * sizeof(usize));
  if (!entries || !queue)
  {
    if (entries)
      OAK_FREE(allocator, entries);
    if (queue)
      OAK_FREE(allocator, queue);
    return 0;
  }

  allocator->collecting_cycles = 1;
  oak_atomic_int_store_relaxed(&allocator->cycle_decrefs, 0);

  /* Seed each candidate with its true refcount and an index into `entries`. */
  usize i = 0;
  for (struct oak_obj_t* obj = allocator->cycle_objects; obj;
       obj = obj->cycle_next)
  {
    entries[i].obj = obj;
    entries[i].external_refs = oak_refcount_load(&obj->refcount);
    entries[i].reachable = 0;
    obj->cycle_index = i++;
  }

  /* Subtract one external ref per owning edge that stays inside the set; what
   * remains is the count of references held from outside the candidate set. */
  struct oak_cycle_scan_ctx_t scan = { allocator, entries };
  for (i = 0; i < count; ++i)
    oak_obj_visit_cycle_edges(entries[i].obj,
                              oak_cycle_subtract_internal_ref,
                              &scan);

  /* Externally rooted candidates, and everything reachable from them, survive. */
  usize head = 0;
  usize tail = 0;
  for (i = 0; i < count; ++i)
  {
    if (entries[i].external_refs > 0)
    {
      entries[i].reachable = 1;
      queue[tail++] = i;
    }
  }
  while (head < tail)
  {
    struct oak_cycle_mark_ctx_t mark = { allocator, entries, queue, &tail };
    oak_obj_visit_cycle_edges(entries[queue[head++]].obj,
                              oak_cycle_mark_child,
                              &mark);
  }

  /* Phase 1: flag the dead set and zero its refcounts so destroy-time decrefs
   * within the set short-circuit instead of recursing or double-freeing. */
  usize collected = 0;
  for (i = 0; i < count; ++i)
  {
    struct oak_obj_t* obj = entries[i].obj;
    if (entries[i].reachable)
      continue;
    oak_obj_unregister_cycle_capable(obj);
    obj->cycle_flags |= OAK_CYCLE_COLLECTING;
    oak_atomic_int_store_relaxed(&obj->refcount.count, 0);
    ++collected;
  }
  /* Phase 2: release each dead object's owned references and buffers. */
  for (i = 0; i < count; ++i)
  {
    struct oak_obj_t* obj = entries[i].obj;
    if (!entries[i].reachable)
      oak_obj_destroy_payload(obj);
  }
  /* Phase 3: free the headers, unless an outstanding weak ref keeps one alive
   * (it will be freed by the last oak_weak_decref). */
  for (i = 0; i < count; ++i)
  {
    struct oak_obj_t* obj = entries[i].obj;
    if (entries[i].reachable)
      continue;
    obj->cycle_flags &= ~OAK_CYCLE_COLLECTING;
    if (oak_refcount_load(&obj->weak_refcount) == 0)
      OAK_FREE(obj->allocator, obj);
  }

  allocator->collecting_cycles = 0;
  OAK_FREE(allocator, queue);
  OAK_FREE(allocator, entries);
  return collected;
}
