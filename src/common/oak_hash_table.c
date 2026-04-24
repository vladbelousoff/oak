#include "oak_hash_table.h"
#include "oak_mem.h"

#include <string.h>

/* FNV-1a 32-bit hash over arbitrary bytes. */
static u32 fnv1a(const void* data, usize len)
{
  const u8* bytes = (const u8*)data;
  u32 h = 2166136261u;
  for (usize i = 0; i < len; ++i)
  {
    h ^= bytes[i];
    h *= 16777619u;
  }
  return h;
}

void oak_hash_table_init(struct oak_hash_table_t* ht)
{
  ht->slots    = null;
  ht->capacity = 0;
  ht->count    = 0;
}

void oak_hash_table_free(struct oak_hash_table_t* ht)
{
  if (ht->slots)
    oak_free(ht->slots, OAK_SRC_LOC);
  ht->slots    = null;
  ht->capacity = 0;
  ht->count    = 0;
}

static void grow(struct oak_hash_table_t* ht)
{
  const int new_cap = ht->capacity < 8 ? 8 : ht->capacity * 2;
  struct oak_hash_table_slot_t* new_slots =
      oak_alloc((usize)new_cap * sizeof *new_slots, OAK_SRC_LOC);
  memset(new_slots, 0, (usize)new_cap * sizeof *new_slots);

  /* Rehash all occupied entries into the new table. */
  for (int i = 0; i < ht->capacity; ++i)
  {
    const struct oak_hash_table_slot_t* s = &ht->slots[i];
    if (!s->key)
      continue;
    int j = (int)(s->hash & (u32)(new_cap - 1));
    while (new_slots[j].key)
      j = (j + 1) & (new_cap - 1);
    new_slots[j] = *s;
  }

  if (ht->slots)
    oak_free(ht->slots, OAK_SRC_LOC);
  ht->slots    = new_slots;
  ht->capacity = new_cap;
}

void oak_hash_table_insert(struct oak_hash_table_t* ht,
                           const void* key,
                           usize       key_len,
                           int         value)
{
  /* Grow before load exceeds 75%. */
  if (ht->count * 4 >= ht->capacity * 3)
    grow(ht);

  const u32 h = fnv1a(key, key_len);
  int i = (int)(h & (u32)(ht->capacity - 1));
  while (ht->slots[i].key)
    i = (i + 1) & (ht->capacity - 1);

  ht->slots[i].key     = key;
  ht->slots[i].key_len = key_len;
  ht->slots[i].hash    = h;
  ht->slots[i].value   = value;
  ht->count++;
}

int oak_hash_table_get(const struct oak_hash_table_t* ht,
                       const void* key,
                       usize       key_len)
{
  if (!ht->capacity)
    return -1;

  const u32 h = fnv1a(key, key_len);
  int i = (int)(h & (u32)(ht->capacity - 1));
  while (ht->slots[i].key)
  {
    if (ht->slots[i].hash    == h        &&
        ht->slots[i].key_len == key_len  &&
        memcmp(ht->slots[i].key, key, key_len) == 0)
      return ht->slots[i].value;
    i = (i + 1) & (ht->capacity - 1);
  }
  return -1;
}
