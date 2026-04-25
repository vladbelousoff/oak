#include "oak_value.h"

#include "oak_bind.h"
#include "oak_log.h"
#include "oak_mem.h"

#include <stdio.h>
#include <string.h>

void oak_obj_incref(struct oak_obj_t* obj)
{
  oak_refcount_inc(&obj->refcount);
}

struct oak_map_entry_t
{
  struct oak_value_t key;
  struct oak_value_t value;
};

void oak_obj_decref(struct oak_obj_t* obj)
{
  if (!oak_refcount_dec(&obj->refcount))
    return;

  if (obj->type == OAK_OBJ_ARRAY)
  {
    struct oak_obj_array_t* arr = (struct oak_obj_array_t*)obj;
    for (usize i = 0; i < arr->length; ++i)
      oak_value_decref(arr->items[i]);
    if (arr->items)
      oak_free(arr->items, OAK_SRC_LOC);
  }
  else if (obj->type == OAK_OBJ_MAP)
  {
    struct oak_obj_map_t* map = (struct oak_obj_map_t*)obj;
    for (usize i = 0; i < map->length; ++i)
    {
      oak_value_decref(map->entries[i].key);
      oak_value_decref(map->entries[i].value);
    }
    if (map->entries)
      oak_free(map->entries, OAK_SRC_LOC);
    if (map->ht)
      oak_free(map->ht, OAK_SRC_LOC);
  }
  else if (obj->type == OAK_OBJ_STRUCT)
  {
    struct oak_obj_struct_t* s = (struct oak_obj_struct_t*)obj;
    for (int i = 0; i < s->field_count; ++i)
      oak_value_decref(s->fields[i]);
  }
  /* OAK_OBJ_NATIVE_STRUCT: neither `instance` nor `type` are owned by Oak;
   * only the wrapper allocation itself is freed. */

  oak_free(obj, OAK_SRC_LOC);
}

static u32 hash_string(const char* chars, const usize length)
{
  u32 hash = 2166136261u;
  for (usize i = 0; i < length; ++i)
  {
    hash ^= (u8)chars[i];
    hash *= 16777619u;
  }
  return hash;
}

struct oak_obj_string_t* oak_string_new(const char* chars, const usize length)
{
  struct oak_obj_string_t* str =
      oak_alloc(sizeof(struct oak_obj_string_t) + length + 1, OAK_SRC_LOC);
  str->obj.type = OAK_OBJ_STRING;
  oak_refcount_init(&str->obj.refcount, 1);
  str->length = length;
  memcpy(str->chars, chars, length);
  str->chars[length] = 0;
  str->hash = hash_string(chars, length);
  return str;
}

struct oak_obj_fn_t* oak_fn_new(const usize code_offset, const int arity)
{
  struct oak_obj_fn_t* fn = oak_alloc(sizeof(struct oak_obj_fn_t), OAK_SRC_LOC);
  fn->obj.type = OAK_OBJ_FN;
  oak_refcount_init(&fn->obj.refcount, 1);
  fn->code_offset = code_offset;
  fn->arity = arity;
  return fn;
}

struct oak_obj_native_fn_t* oak_native_fn_new(const oak_native_fn_t fn,
                                              const int arity,
                                              const char* name)
{
  struct oak_obj_native_fn_t* native =
      oak_alloc(sizeof(struct oak_obj_native_fn_t), OAK_SRC_LOC);
  native->obj.type = OAK_OBJ_NATIVE_FN;
  oak_refcount_init(&native->obj.refcount, 1);
  native->fn = fn;
  native->arity = arity;
  native->name = name;
  return native;
}

int oak_native_fn_format(char* buf,
                         const usize size,
                         const struct oak_obj_native_fn_t* native)
{
  const void* fn_ptr = (const void*)(uintptr_t)native->fn;
  if (native->name && native->name[0] != '\0')
    return snprintf(buf,
                    size,
                    "<native %s arity=%d fn=%p>",
                    native->name,
                    native->arity,
                    fn_ptr);
  return snprintf(buf, size, "<native arity=%d fn=%p>", native->arity, fn_ptr);
}

struct oak_obj_array_t* oak_array_new(void)
{
  struct oak_obj_array_t* arr =
      oak_alloc(sizeof(struct oak_obj_array_t), OAK_SRC_LOC);
  arr->obj.type = OAK_OBJ_ARRAY;
  oak_refcount_init(&arr->obj.refcount, 1);
  arr->length = 0;
  arr->capacity = 0;
  arr->items = null;
  return arr;
}

void oak_array_push(struct oak_obj_array_t* arr, const struct oak_value_t value)
{
  if (arr->length >= arr->capacity)
  {
    const usize new_cap = arr->capacity == 0 ? 8u : arr->capacity * 2u;
    arr->items = oak_realloc(
        arr->items, new_cap * sizeof(struct oak_value_t), OAK_SRC_LOC);
    arr->capacity = new_cap;
  }
  oak_value_incref(value);
  arr->items[arr->length++] = value;
}

struct oak_obj_struct_t* oak_struct_new(const int field_count,
                                        const char* type_name)
{
  oak_assert(field_count >= 0);
  const usize size = sizeof(struct oak_obj_struct_t) +
                     (usize)field_count * sizeof(struct oak_value_t);
  struct oak_obj_struct_t* s = oak_alloc(size, OAK_SRC_LOC);
  s->obj.type = OAK_OBJ_STRUCT;
  oak_refcount_init(&s->obj.refcount, 1);
  s->type_name = type_name;
  s->field_count = field_count;
  for (int i = 0; i < field_count; ++i)
    s->fields[i] = OAK_VALUE_I32(0);
  return s;
}

struct oak_obj_native_struct_t*
oak_obj_native_struct_new(const struct oak_native_type_t* type, void* instance)
{
  struct oak_obj_native_struct_t* ns =
      oak_alloc(sizeof(struct oak_obj_native_struct_t), OAK_SRC_LOC);
  ns->obj.type = OAK_OBJ_NATIVE_STRUCT;
  oak_refcount_init(&ns->obj.refcount, 1);
  ns->instance = instance;
  ns->type = type;
  return ns;
}

struct oak_obj_map_t* oak_map_new(void)
{
  struct oak_obj_map_t* map =
      oak_alloc(sizeof(struct oak_obj_map_t), OAK_SRC_LOC);
  map->obj.type = OAK_OBJ_MAP;
  oak_refcount_init(&map->obj.refcount, 1);
  map->length = 0;
  map->capacity = 0;
  map->entries = null;
  map->ht_capacity = 0;
  map->ht = null;
  return map;
}

/* FNV-1a-inspired hash for a runtime value. */
static u32 hash_value(const struct oak_value_t v)
{
  switch (v.type)
  {
    case OAK_VAL_BOOL:
      return (u32)oak_as_bool(v) * 2654435761u;
    case OAK_VAL_NUMBER:
      if (oak_is_f32(v))
      {
        float f = oak_as_f32(v);
        u32 bits = 0;
        memcpy(&bits, &f, sizeof(bits));
        return bits * 2654435761u;
      }
      return (u32)oak_as_i32(v) * 2654435761u;
    case OAK_VAL_OBJ:
      if (oak_is_string(v))
        return oak_as_string(v)->hash;
      return (u32)(uintptr_t)oak_as_obj(v) * 2654435761u;
  }
  return 0;
}

/* Open-addressing probe (linear).  Returns the ht slot where the key was
 * found or the best insertion slot (first tombstone, or first empty).
 * *out_idx is set to the entries[] index when found, or MAP_HT_EMPTY. */
static usize ht_probe(const usize* ht,
                      const usize ht_cap,
                      const struct oak_map_entry_t* entries,
                      const struct oak_value_t key,
                      usize* out_idx)
{
  const u32 hash = hash_value(key);
  usize slot = (usize)(hash & (u32)(ht_cap - 1u));
  usize first_tomb = MAP_HT_EMPTY;
  for (;;)
  {
    const usize idx = ht[slot];
    if (idx == MAP_HT_EMPTY)
    {
      *out_idx = MAP_HT_EMPTY;
      return (first_tomb != MAP_HT_EMPTY) ? first_tomb : slot;
    }
    if (idx == MAP_HT_TOMBSTONE)
    {
      if (first_tomb == MAP_HT_EMPTY)
        first_tomb = slot;
    }
    else if (oak_value_equal(entries[idx].key, key))
    {
      *out_idx = idx;
      return slot;
    }
    slot = (slot + 1u) & (ht_cap - 1u);
  }
}

/* Rebuild the hash table with a new capacity (must be a power of two). */
static void map_ht_rebuild(struct oak_obj_map_t* map, const usize new_cap)
{
  usize* new_ht = oak_alloc(new_cap * sizeof(usize), OAK_SRC_LOC);
  for (usize i = 0; i < new_cap; ++i)
    new_ht[i] = MAP_HT_EMPTY;

  for (usize i = 0; i < map->length; ++i)
  {
    const u32 hash = hash_value(map->entries[i].key);
    usize slot = (usize)(hash & (u32)(new_cap - 1u));
    while (new_ht[slot] != MAP_HT_EMPTY)
      slot = (slot + 1u) & (new_cap - 1u);
    new_ht[slot] = i;
  }

  if (map->ht)
    oak_free(map->ht, OAK_SRC_LOC);
  map->ht = new_ht;
  map->ht_capacity = new_cap;
}

int oak_map_get(const struct oak_obj_map_t* map,
                const struct oak_value_t key,
                struct oak_value_t* out)
{
  if (!map->ht || map->length == 0)
    return 0;
  usize entry_idx;
  ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);
  if (entry_idx == MAP_HT_EMPTY)
    return 0;
  if (out)
    *out = map->entries[entry_idx].value;
  return 1;
}

int oak_map_has(const struct oak_obj_map_t* map, const struct oak_value_t key)
{
  if (!map->ht || map->length == 0)
    return 0;
  usize entry_idx;
  ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);
  return entry_idx != MAP_HT_EMPTY;
}

int oak_map_delete(struct oak_obj_map_t* map, const struct oak_value_t key)
{
  if (!map->ht || map->length == 0)
    return 0;

  usize entry_idx;
  const usize del_slot =
      ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);
  if (entry_idx == MAP_HT_EMPTY)
    return 0;

  const struct oak_value_t del_key = map->entries[entry_idx].key;
  const struct oak_value_t del_val = map->entries[entry_idx].value;

  /* Mark the hash table slot as deleted. */
  map->ht[del_slot] = MAP_HT_TOMBSTONE;

  /* Compact the dense entries array. */
  const usize last = map->length - 1u;
  if (entry_idx != last)
  {
    map->entries[entry_idx] = map->entries[last];

    /* Update the ht slot that was pointing to `last` so it points to the
     * entry's new position.  We probe for the moved entry's key; del_slot is
     * now a tombstone so the probe correctly skips it and reaches the original
     * slot that held `last`. */
    usize moved_idx;
    const usize moved_slot = ht_probe(map->ht,
                                      map->ht_capacity,
                                      map->entries,
                                      map->entries[entry_idx].key,
                                      &moved_idx);
    (void)moved_idx;
    map->ht[moved_slot] = entry_idx;
  }

  map->length--;
  oak_value_decref(del_key);
  oak_value_decref(del_val);
  return 1;
}

struct oak_value_t oak_map_key_at(const struct oak_obj_map_t* map,
                                  const usize index)
{
  oak_assert(index < map->length);
  return map->entries[index].key;
}

struct oak_value_t oak_map_value_at(const struct oak_obj_map_t* map,
                                    const usize index)
{
  oak_assert(index < map->length);
  return map->entries[index].value;
}

void oak_map_set(struct oak_obj_map_t* map,
                 const struct oak_value_t key,
                 const struct oak_value_t value)
{
  /* Grow hash table before inserting so the load factor stays below 75 %. */
  if (!map->ht || (map->length + 1u) * 4u > map->ht_capacity * 3u)
  {
    const usize new_cap = map->ht_capacity < 8u ? 8u : map->ht_capacity * 2u;
    map_ht_rebuild(map, new_cap);
  }

  usize entry_idx;
  const usize slot =
      ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);

  if (entry_idx != MAP_HT_EMPTY)
  {
    /* Update existing entry in-place. */
    oak_value_incref(value);
    oak_value_decref(map->entries[entry_idx].value);
    map->entries[entry_idx].value = value;
    return;
  }

  /* Grow the dense entries array if needed. */
  if (map->length >= map->capacity)
  {
    const usize new_cap = map->capacity == 0u ? 8u : map->capacity * 2u;
    map->entries = oak_realloc(
        map->entries, new_cap * sizeof(struct oak_map_entry_t), OAK_SRC_LOC);
    map->capacity = new_cap;
  }

  oak_value_incref(key);
  oak_value_incref(value);
  map->entries[map->length].key = key;
  map->entries[map->length].value = value;
  map->ht[slot] = map->length;
  map->length++;
}

struct oak_obj_string_t* oak_string_concat(const struct oak_obj_string_t* a,
                                           const struct oak_obj_string_t* b)
{
  const usize length = a->length + b->length;
  struct oak_obj_string_t* str =
      oak_alloc(sizeof(struct oak_obj_string_t) + length + 1, OAK_SRC_LOC);
  str->obj.type = OAK_OBJ_STRING;
  oak_refcount_init(&str->obj.refcount, 1);
  str->length = length;
  memcpy(str->chars, a->chars, a->length);
  memcpy(str->chars + a->length, b->chars, b->length);
  str->chars[length] = 0;
  str->hash = hash_string(str->chars, length);
  return str;
}

int oak_is_truthy(const struct oak_value_t value)
{
  if (oak_is_bool(value))
    return oak_as_bool(value);
  if (oak_is_number(value))
  {
    if (oak_is_f32(value))
      return oak_as_f32(value) != 0.0f;
    return oak_as_i32(value) != 0;
  }
  if (oak_is_obj(value))
    return 1;
  return 0;
}

int oak_value_equal(const struct oak_value_t a, const struct oak_value_t b)
{
  if (a.type != b.type)
    return 0;

  switch (a.type)
  {
    case OAK_VAL_BOOL:
      return oak_as_bool(a) == oak_as_bool(b);
    case OAK_VAL_NUMBER:
      if (oak_is_f32(a) != oak_is_f32(b))
        return 0;
      if (oak_is_f32(a))
        return oak_as_f32(a) == oak_as_f32(b);
      return oak_as_i32(a) == oak_as_i32(b);
    case OAK_VAL_OBJ:
      if (oak_is_string(a) && oak_is_string(b))
      {
        const struct oak_obj_string_t* str_a = oak_as_string(a);
        const struct oak_obj_string_t* str_b = oak_as_string(b);
        if (str_a->length != str_b->length)
          return 0;
        if (str_a->hash != str_b->hash)
          return 0;
        return memcmp(str_a->chars, str_b->chars, str_a->length) == 0;
      }
      if (oak_is_fn(a) && oak_is_fn(b))
        return oak_as_obj(a) == oak_as_obj(b);
      if (oak_is_native_fn(a) && oak_is_native_fn(b))
        return oak_as_obj(a) == oak_as_obj(b);
      return oak_as_obj(a) == oak_as_obj(b);
  }

  return 0;
}

void oak_value_incref(const struct oak_value_t value)
{
  if (oak_is_obj(value))
    oak_obj_incref(oak_as_obj(value));
}

void oak_value_decref(const struct oak_value_t value)
{
  if (oak_is_obj(value))
    oak_obj_decref(oak_as_obj(value));
}

/* Renders value into buf (no newline). buf must be at least 1 byte. */
static void
oak_value_format(const struct oak_value_t value, char* buf, const usize size)
{
  if (oak_is_bool(value))
  {
    snprintf(buf, size, "%s", oak_as_bool(value) ? "true" : "false");
    return;
  }
  if (oak_is_number(value))
  {
    if (oak_is_f32(value))
      snprintf(buf, size, "%f", oak_as_f32(value));
    else
      snprintf(buf, size, "%d", oak_as_i32(value));
    return;
  }
  if (oak_is_obj(value))
  {
    if (oak_is_string(value))
      snprintf(buf, size, "%s", oak_as_cstring(value));
    else if (oak_is_fn(value))
      snprintf(buf, size, "<fn @%zu>", oak_as_fn(value)->code_offset);
    else if (oak_is_native_fn(value))
      oak_native_fn_format(buf, size, oak_as_native_fn(value));
    else if (oak_is_array(value))
      snprintf(buf, size, "<array len=%zu>", oak_as_array(value)->length);
    else if (oak_is_map(value))
      snprintf(buf, size, "<map len=%zu>", oak_as_map(value)->length);
    else if (oak_is_struct(value))
    {
      const struct oak_obj_struct_t* s = oak_as_struct(value);
      snprintf(buf,
               size,
               "<%s fields=%d>",
               s->type_name ? s->type_name : "struct",
               s->field_count);
    }
    else if (oak_is_native_struct(value))
    {
      const struct oak_obj_native_struct_t* ns = oak_as_native_struct(value);
      snprintf(buf,
               size,
               "<native %s %p>",
               ns->type ? ns->type->name : "struct",
               ns->instance);
    }
    else
      snprintf(buf, size, "%p", (void*)oak_as_obj(value));
    return;
  }
  buf[0] = '\0';
}

#define OAK_PRINT_BUF_SIZE 4096

void oak_value_print(const struct oak_value_t value)
{
  char buf[OAK_PRINT_BUF_SIZE];
  oak_value_format(value, buf, sizeof(buf));
  fputs(buf, stdout);
}

void oak_value_println(const struct oak_value_t value)
{
  char buf[OAK_PRINT_BUF_SIZE + 1];
  oak_value_format(value, buf, OAK_PRINT_BUF_SIZE);
  const usize len = strlen(buf);
  buf[len] = '\n';
  buf[len + 1] = '\0';
  fputs(buf, stdout);
}
