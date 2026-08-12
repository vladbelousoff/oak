#include "internal/oak_hash_table.h"

#include "oak_allocator.h"

#include <stdint.h>
#include <string.h>

/* A slot's key is null when the slot was never used, and points at this
 * marker when the entry was removed. Probing must walk past tombstones but
 * stop at never-used slots, which is what keeps lookups correct after a
 * removal. */
static const char tombstone_marker;
#define TOMBSTONE ((const void*)&tombstone_marker)

/* Returned by the probe helpers for "no such slot". Capacity can never reach
 * SIZE_MAX, so this cannot collide with a real index. */
#define NO_SLOT ((usize)SIZE_MAX)

typedef struct oak_hash_slot oak_hash_slot_t;
struct oak_hash_slot
{
  const void* key; /* null = never used, TOMBSTONE = removed */
  usize key_len;
  u32 hash;
};

/* No embedded base: the handle points straight at this body. Slots and values
 * are separate allocations, so a rehash never moves the handle. */
typedef struct oak_hash_table oak_hash_table_t;
struct oak_hash_table
{
  oak_hash_slot_t* slots;
  u8* values;     /* capacity * value_size bytes; null when value_size is 0 */
  usize capacity; /* power of two, or 0 when unallocated */
  usize count;    /* live entries, excluding tombstones */
  usize tombstones;
  usize value_size;
};

static oak_hash_table_t* as_table(oak_container_t* c)
{
  return (oak_hash_table_t*)c;
}

static const oak_hash_table_t* as_ctable(const oak_container_t* c)
{
  return (const oak_hash_table_t*)c;
}

static int slot_matches(const oak_hash_slot_t* s,
                        const void* key,
                        usize key_len,
                        u32 hash)
{
  return s->key && s->key != TOMBSTONE && s->hash == hash &&
         s->key_len == key_len && memcmp(s->key, key, key_len) == 0;
}

/* Index of the live entry for `key`, or NO_SLOT. */
static usize find_slot(const oak_hash_table_t* t,
                       const void* key,
                       usize key_len,
                       u32 hash)
{
  if (t->capacity == 0)
    return NO_SLOT;

  const usize mask = t->capacity - 1;
  usize i = hash & mask;
  for (;;)
  {
    const oak_hash_slot_t* s = &t->slots[i];
    if (!s->key)
      return NO_SLOT;
    if (slot_matches(s, key, key_len, hash))
      return i;
    i = (i + 1) & mask;
  }
}

/* Reallocates to `new_capacity` slots and rehashes live entries, dropping
 * tombstones. Leaves the table untouched and returns 0 on failure. */
static int rehash(oak_hash_table_t* t, usize new_capacity)
{
  if (new_capacity > SIZE_MAX / sizeof(oak_hash_slot_t))
    return 0;
  if (t->value_size && new_capacity > SIZE_MAX / t->value_size)
    return 0;

  oak_allocator_t* allocator = oak_container_allocator((oak_container_t*)t);
  oak_hash_slot_t* new_slots =
      OAK_ALLOC(allocator, new_capacity * sizeof *new_slots);
  if (!new_slots)
    return 0;
  memset(new_slots, 0, new_capacity * sizeof *new_slots);

  u8* new_values = null;
  if (t->value_size)
  {
    new_values = OAK_ALLOC(allocator, new_capacity * t->value_size);
    if (!new_values)
    {
      OAK_FREE(allocator, new_slots);
      return 0;
    }
  }

  const usize mask = new_capacity - 1;
  for (usize i = 0; i < t->capacity; ++i)
  {
    const oak_hash_slot_t* s = &t->slots[i];
    if (!s->key || s->key == TOMBSTONE)
      continue;
    usize j = s->hash & mask;
    while (new_slots[j].key)
      j = (j + 1) & mask;
    new_slots[j] = *s;
    if (t->value_size)
      memcpy(new_values + j * t->value_size,
             t->values + i * t->value_size,
             t->value_size);
  }

  if (t->slots)
    OAK_FREE(allocator, t->slots);
  if (t->values)
    OAK_FREE(allocator, t->values);
  t->slots = new_slots;
  t->values = new_values;
  t->capacity = new_capacity;
  t->tombstones = 0;
  return 1;
}

/* Keeps occupancy — live entries plus tombstones — below 75%, so probing
 * always terminates on a never-used slot. */
static int ensure_room(oak_hash_table_t* t)
{
  if ((t->count + t->tombstones + 1) * 4 < t->capacity * 3)
    return 1;
  const usize capacity = t->capacity < 8 ? 8 : t->capacity * 2;
  if (capacity <= t->capacity && t->capacity != 0)
    return 0; /* usize overflow */
  return rehash(t, capacity);
}

/* Inserts or overwrites. `replace_existing` distinguishes map put (overwrite)
 * from set add (leave the existing entry alone and report it). */
static int table_store(oak_hash_table_t* t,
                       const void* key,
                       usize key_len,
                       const void* value,
                       int replace_existing)
{
  if (!key)
    return 0;
  if (t->value_size && !value)
    return 0;
  if (!ensure_room(t))
    return 0;

  const u32 hash = oak_hash_bytes(key, key_len);
  const usize mask = t->capacity - 1;
  usize i = hash & mask;
  usize first_tombstone = NO_SLOT;

  for (;;)
  {
    oak_hash_slot_t* s = &t->slots[i];
    if (!s->key)
      break;
    if (s->key == TOMBSTONE)
    {
      if (first_tombstone == NO_SLOT)
        first_tombstone = i;
    }
    else if (slot_matches(s, key, key_len, hash))
    {
      if (!replace_existing)
        return 0;
      /* Adopt the caller's key pointer: the previous one may be about to go
       * out of scope, and both compare equal by content anyway. */
      s->key = key;
      if (t->value_size)
        memcpy(t->values + i * t->value_size, value, t->value_size);
      return 1;
    }
    i = (i + 1) & mask;
  }

  if (first_tombstone != NO_SLOT)
  {
    i = first_tombstone;
    --t->tombstones;
  }
  t->slots[i].key = key;
  t->slots[i].key_len = key_len;
  t->slots[i].hash = hash;
  if (t->value_size)
    memcpy(t->values + i * t->value_size, value, t->value_size);
  ++t->count;
  return 1;
}

/* ---------- vtable slots ---------- */

usize oak_hash_table_size(const oak_container_t* c)
{
  return as_ctable(c)->count;
}

void oak_hash_table_clear(oak_container_t* c)
{
  oak_hash_table_t* t = as_table(c);
  if (t->slots)
    memset(t->slots, 0, t->capacity * sizeof *t->slots);
  t->count = 0;
  t->tombstones = 0;
}

void* oak_hash_table_find(oak_container_t* c,
                          const void* key,
                          usize key_len)
{
  oak_hash_table_t* t = as_table(c);
  if (!key || t->value_size == 0)
    return null;
  const usize i = find_slot(t, key, key_len, oak_hash_bytes(key, key_len));
  return i == NO_SLOT ? null : t->values + i * t->value_size;
}

int oak_hash_table_put(oak_container_t* c,
                       const void* key,
                       usize key_len,
                       const void* value)
{
  return table_store(as_table(c), key, key_len, value, 1);
}

int oak_hash_table_add(oak_container_t* c,
                       const void* value,
                       usize value_len)
{
  return table_store(as_table(c), value, value_len, null, 0);
}

int oak_hash_table_erase_key(oak_container_t* c,
                             const void* key,
                             usize key_len)
{
  oak_hash_table_t* t = as_table(c);
  if (!key)
    return 0;
  const usize i = find_slot(t, key, key_len, oak_hash_bytes(key, key_len));
  if (i == NO_SLOT)
    return 0;
  t->slots[i].key = TOMBSTONE;
  --t->count;
  ++t->tombstones;
  return 1;
}

int oak_hash_table_contains(const oak_container_t* c,
                            const void* key,
                            usize key_len)
{
  const oak_hash_table_t* t = as_ctable(c);
  if (!key)
    return 0;
  return find_slot(t, key, key_len, oak_hash_bytes(key, key_len)) != NO_SLOT;
}

/* ---------- iteration ---------- */

/* Advances `index` to the next live slot, clearing `owner` when there is
 * none left. Returns 1 while the cursor is on an entry. */
static int seek_live(oak_iterator_t* it, usize from)
{
  oak_hash_table_t* t = as_table((oak_container_t*)it->owner);
  for (usize i = from; i < t->capacity; ++i)
  {
    if (t->slots[i].key && t->slots[i].key != TOMBSTONE)
    {
      it->state.index = i;
      return 1;
    }
  }
  it->owner = null;
  return 0;
}

oak_iterator_t oak_hash_table_begin(oak_container_t* c)
{
  oak_iterator_t it = { .owner = c };
  it.state.index = 0;
  seek_live(&it, 0);
  return it;
}

int oak_hash_table_next(oak_iterator_t* it)
{
  return seek_live(it, it->state.index + 1);
}

void* oak_hash_table_iter_get(oak_iterator_t* it)
{
  oak_hash_table_t* t = as_table((oak_container_t*)it->owner);
  if (t->value_size == 0)
    return null;
  return t->values + it->state.index * t->value_size;
}

const void* oak_hash_table_iter_key(oak_iterator_t* it,
                                    usize* out_key_len)
{
  oak_hash_table_t* t = as_table((oak_container_t*)it->owner);
  const oak_hash_slot_t* s = &t->slots[it->state.index];
  if (out_key_len)
    *out_key_len = s->key_len;
  return s->key;
}

/* ---------- lifetime ---------- */

void oak_hash_table_destroy(void* obj)
{
  oak_hash_table_t* t = (oak_hash_table_t*)obj;
  oak_allocator_t* allocator = oak_object_header(obj)->allocator;
  if (t->slots)
    OAK_FREE(allocator, t->slots);
  if (t->values)
    OAK_FREE(allocator, t->values);
  oak_object_free(obj);
}

oak_container_t* oak_hash_table_new(
    oak_allocator_t* allocator,
    usize value_size,
    const oak_container_vtable_t* vt)
{
  if (!allocator)
    return null;

  oak_hash_table_t* t = oak_object_alloc(allocator, sizeof *t, &vt->object);
  if (!t)
    return null;

  t->slots = null;
  t->values = null;
  t->capacity = 0;
  t->count = 0;
  t->tombstones = 0;
  t->value_size = value_size;
  return (oak_container_t*)t;
}

usize oak_hash_table_value_size(const oak_container_t* c)
{
  return as_ctable(c)->value_size;
}
