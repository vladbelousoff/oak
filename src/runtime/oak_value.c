#include "oak_value.h"

#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void oak_obj_incref(struct oak_obj_t* obj)
{
  oak_refcount_inc(&obj->refcount);
}

static void oak_obj_destroy_payload(struct oak_obj_t* obj)
{
  struct oak_allocator_t* a = obj->allocator;

  if (obj->type == OAK_OBJ_ARRAY)
  {
    struct oak_obj_array_t* arr = (struct oak_obj_array_t*)obj;
    for (usize i = 0; i < arr->length; ++i)
      oak_value_decref(arr->items[i]);
    if (arr->items)
      OAK_FREE(a, arr->items);
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
      OAK_FREE(a, map->entries);
    if (map->ht)
      OAK_FREE(a, map->ht);
  }
  else if (obj->type == OAK_OBJ_RECORD)
  {
    struct oak_obj_record_t* s = (struct oak_obj_record_t*)obj;
    if (s->field_name_storage)
      OAK_FREE(a, s->field_name_storage);
    for (int i = 0; i < s->field_count; ++i)
      oak_value_decref(s->fields[i]);
  }
  else if (obj->type == OAK_OBJ_NATIVE_RECORD)
  {
    struct oak_obj_native_record_t* ns = (struct oak_obj_native_record_t*)obj;
    if (ns->instance && ns->type && ns->type->destructor)
      ns->type->destructor(ns->instance);
  }
  else if (obj->type == OAK_OBJ_TRAIT_OBJECT)
  {
    struct oak_obj_trait_object_t* to = (struct oak_obj_trait_object_t*)obj;
    oak_value_decref(to->value);
    oak_obj_decref((struct oak_obj_t*)to->vtable);
  }
  else if (obj->type == OAK_OBJ_FN)
  {
    struct oak_obj_fn_t* fn = (struct oak_obj_fn_t*)obj;
    if (fn->name)
      OAK_FREE(a, (void*)fn->name);
    if (fn->attr_hooks)
      OAK_FREE(a, fn->attr_hooks);
  }
  else if (obj->type == OAK_OBJ_NATIVE_FN)
  {
    struct oak_obj_native_fn_t* nfn = (struct oak_obj_native_fn_t*)obj;
    if (nfn->attr_hooks)
      OAK_FREE(a, nfn->attr_hooks);
  }
}

void oak_obj_decref(struct oak_obj_t* obj)
{
  if (!oak_refcount_dec(&obj->refcount))
    return;

  oak_refcount_inc(&obj->weak_refcount);
  oak_obj_destroy_payload(obj);
  if (!oak_refcount_dec(&obj->weak_refcount))
    return;

  OAK_FREE(obj->allocator, obj);
}

void oak_weak_decref(struct oak_obj_t* obj)
{
  if (!oak_refcount_dec(&obj->weak_refcount))
    return;
  if (obj->refcount.count == 0)
    OAK_FREE(obj->allocator, obj);
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

struct oak_obj_string_t* oak_string_new(struct oak_allocator_t* a,
                                        const char* chars,
                                        const usize length)
{
  struct oak_obj_string_t* str =
      OAK_ALLOC(a, sizeof(struct oak_obj_string_t) + length + 1);
  str->obj.type = OAK_OBJ_STRING;
  oak_refcount_init(&str->obj.refcount, 1);
  oak_refcount_init(&str->obj.weak_refcount, 0);
  str->obj.allocator = a;
  str->length = length;
  memcpy(str->chars, chars, length);
  str->chars[length] = 0;
  str->hash = hash_string(chars, length);
  return str;
}

struct oak_obj_fn_t* oak_fn_new(struct oak_allocator_t* a,
                                const usize code_offset,
                                const int arity,
                                const u16 module_id)
{
  struct oak_obj_fn_t* fn = OAK_ALLOC(a, sizeof(struct oak_obj_fn_t));
  fn->obj.type = OAK_OBJ_FN;
  oak_refcount_init(&fn->obj.refcount, 1);
  oak_refcount_init(&fn->obj.weak_refcount, 0);
  fn->obj.allocator = a;
  fn->code_offset = code_offset;
  fn->arity = arity;
  fn->module_id = module_id;
  fn->name = null;
  fn->attr_hooks = null;
  fn->attr_hook_count = 0;
  return fn;
}

struct oak_obj_native_fn_t*
oak_native_fn_new(struct oak_allocator_t* a,
                  const oak_native_fn_t fn,
                  const int arity,
                  const char* name)
{
  struct oak_obj_native_fn_t* native =
      OAK_ALLOC(a, sizeof(struct oak_obj_native_fn_t));
  native->obj.type = OAK_OBJ_NATIVE_FN;
  oak_refcount_init(&native->obj.refcount, 1);
  oak_refcount_init(&native->obj.weak_refcount, 0);
  native->obj.allocator = a;
  native->fn = fn;
  native->arity = arity;
  native->name = name;
  native->attr_hooks = null;
  native->attr_hook_count = 0;
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

struct oak_obj_array_t* oak_array_new(struct oak_allocator_t* a)
{
  struct oak_obj_array_t* arr =
      OAK_ALLOC(a, sizeof(struct oak_obj_array_t));
  arr->obj.type = OAK_OBJ_ARRAY;
  oak_refcount_init(&arr->obj.refcount, 1);
  oak_refcount_init(&arr->obj.weak_refcount, 0);
  arr->obj.allocator = a;
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
    arr->items = OAK_REALLOC(arr->obj.allocator,
        arr->items, new_cap * sizeof(struct oak_value_t));
    arr->capacity = new_cap;
  }
  oak_value_incref(value);
  arr->items[arr->length++] = value;
}

struct oak_obj_record_t* oak_record_new(struct oak_allocator_t* a,
                                        const int field_count,
                                        const char* const type_name,
                                        const char* const* const field_names,
                                        const usize* const field_name_len)
{
  oak_assert(field_count >= 0);
  const usize size = sizeof(struct oak_obj_record_t) +
                     (usize)field_count * sizeof(struct oak_value_t);
  struct oak_obj_record_t* s = OAK_ALLOC(a, size);
  s->obj.type = OAK_OBJ_RECORD;
  oak_refcount_init(&s->obj.refcount, 1);
  oak_refcount_init(&s->obj.weak_refcount, 0);
  s->obj.allocator = a;
  s->type_name = type_name;
  s->field_count = field_count;
  s->field_name_ptrs = null;
  s->field_name_storage = null;
  for (int i = 0; i < field_count; ++i)
    s->fields[i] = OAK_VALUE_I32(0);
  if (field_names && field_count > 0)
  {
    usize strings_total = 0u;
    for (int i = 0; i < field_count; ++i)
    {
      const usize n =
          field_name_len ? field_name_len[i] : strlen(field_names[i]);
      strings_total += n + 1u;
    }
    const usize blob = (usize)field_count * sizeof(const char*) + strings_total;
    char* const raw = OAK_ALLOC(a, blob);
    s->field_name_storage = raw;
    const char** const ptrs = (const char**)raw;
    s->field_name_ptrs = (const char* const*)ptrs;
    char* p = raw + (usize)field_count * (usize)sizeof(const char*);
    for (int i = 0; i < field_count; ++i)
    {
      const usize n =
          field_name_len ? field_name_len[i] : strlen(field_names[i]);
      memcpy(p, field_names[i], n);
      p[n] = '\0';
      ptrs[i] = p;
      p += n + 1u;
    }
  }
  return s;
}

struct oak_obj_native_record_t*
oak_obj_native_record_new(struct oak_allocator_t* a,
                          const struct oak_bind_type_t* type,
                          void* instance)
{
  struct oak_obj_native_record_t* ns =
      OAK_ALLOC(a, sizeof(struct oak_obj_native_record_t));
  ns->obj.type = OAK_OBJ_NATIVE_RECORD;
  oak_refcount_init(&ns->obj.refcount, 1);
  oak_refcount_init(&ns->obj.weak_refcount, 0);
  ns->obj.allocator = a;
  ns->instance = instance;
  ns->type = type;
  return ns;
}

struct oak_obj_trait_object_t*
oak_trait_object_new(struct oak_allocator_t* a,
                     struct oak_value_t value,
                     struct oak_obj_array_t* vtable)
{
  struct oak_obj_trait_object_t* to =
      OAK_ALLOC(a, sizeof(struct oak_obj_trait_object_t));
  to->obj.type = OAK_OBJ_TRAIT_OBJECT;
  oak_refcount_init(&to->obj.refcount, 1);
  oak_refcount_init(&to->obj.weak_refcount, 0);
  to->obj.allocator = a;
  oak_value_incref(value);
  to->value = value;
  oak_obj_incref((struct oak_obj_t*)vtable);
  to->vtable = vtable;
  return to;
}

struct oak_obj_map_t* oak_map_new(struct oak_allocator_t* a)
{
  struct oak_obj_map_t* map =
      OAK_ALLOC(a, sizeof(struct oak_obj_map_t));
  map->obj.type = OAK_OBJ_MAP;
  oak_refcount_init(&map->obj.refcount, 1);
  oak_refcount_init(&map->obj.weak_refcount, 0);
  map->obj.allocator = a;
  map->length = 0;
  map->capacity = 0;
  map->entries = null;
  map->ht_capacity = 0;
  map->ht = null;
  return map;
}

static u32 hash_value(const struct oak_value_t v)
{
  if (oak_is_bool(v))
    return (u32)oak_as_bool(v) * 2654435761u;
  if (oak_is_i32(v))
    return (u32)oak_as_i32(v) * 2654435761u;
  if (oak_is_f32(v))
    return oak_f32_to_bits(oak_as_f32(v)) * 2654435761u;
  if (oak_is_none(v))
    return 0x9E3779B9u;
  if (oak_is_string(v))
    return oak_as_string(v)->hash;
  {
    struct oak_obj_t* p = oak_val_obj_ptr(v);
    return (u32)(uintptr_t)p * 2654435761u;
  }
}

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

static void map_ht_rebuild(struct oak_obj_map_t* map, const usize new_cap)
{
  struct oak_allocator_t* a = map->obj.allocator;
  usize* new_ht = OAK_ALLOC(a, new_cap * sizeof(usize));
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
    OAK_FREE(a, map->ht);
  map->ht = new_ht;
  map->ht_capacity = new_cap;
}

int oak_map_get(const struct oak_obj_map_t* map,
                const struct oak_value_t key,
                struct oak_value_t* out)
{
  if (oak_is_none(key) || oak_is_weak_obj(key))
    return 0;
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
  if (oak_is_none(key) || oak_is_weak_obj(key))
    return 0;
  if (!map->ht || map->length == 0)
    return 0;
  usize entry_idx;
  ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);
  return entry_idx != MAP_HT_EMPTY;
}

int oak_map_delete(struct oak_obj_map_t* map, const struct oak_value_t key)
{
  if (oak_is_none(key) || oak_is_weak_obj(key))
    return 0;
  if (!map->ht || map->length == 0)
    return 0;

  usize entry_idx;
  const usize del_slot =
      ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);
  if (entry_idx == MAP_HT_EMPTY)
    return 0;

  const struct oak_value_t del_key = map->entries[entry_idx].key;
  const struct oak_value_t del_val = map->entries[entry_idx].value;

  map->ht[del_slot] = MAP_HT_TOMBSTONE;

  const usize last = map->length - 1u;
  if (entry_idx != last)
  {
    map->entries[entry_idx] = map->entries[last];

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

int oak_map_set(struct oak_obj_map_t* map,
                const struct oak_value_t key,
                const struct oak_value_t value)
{
  if (oak_is_weak_obj(key) || oak_is_none(key))
    return 0;
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
    oak_value_incref(value);
    oak_value_decref(map->entries[entry_idx].value);
    map->entries[entry_idx].value = value;
    return 1;
  }

  if (map->length >= map->capacity)
  {
    const usize new_cap = map->capacity == 0u ? 8u : map->capacity * 2u;
    map->entries = OAK_REALLOC(map->obj.allocator,
        map->entries, new_cap * sizeof(struct oak_map_entry_t));
    map->capacity = new_cap;
  }

  oak_value_incref(key);
  oak_value_incref(value);
  map->entries[map->length].key = key;
  map->entries[map->length].value = value;
  map->ht[slot] = map->length;
  map->length++;
  return 1;
}

struct oak_obj_string_t* oak_string_concat(struct oak_allocator_t* al,
                                           const struct oak_obj_string_t* a,
                                           const struct oak_obj_string_t* b)
{
  const usize length = a->length + b->length;
  struct oak_obj_string_t* str =
      OAK_ALLOC(al, sizeof(struct oak_obj_string_t) + length + 1);
  str->obj.type = OAK_OBJ_STRING;
  oak_refcount_init(&str->obj.refcount, 1);
  oak_refcount_init(&str->obj.weak_refcount, 0);
  str->obj.allocator = al;
  str->length = length;
  memcpy(str->chars, a->chars, a->length);
  memcpy(str->chars + a->length, b->chars, b->length);
  str->chars[length] = 0;
  str->hash = hash_string(str->chars, length);
  return str;
}

int oak_is_truthy(const struct oak_value_t value)
{
  if (oak_is_none_like(value))
    return 0;
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
  const int a_none = oak_is_none_like(a);
  const int b_none = oak_is_none_like(b);
  if (a_none || b_none)
    return a_none && b_none;

  if (oak_is_obj(a) && oak_is_obj(b))
  {
    if (oak_is_string(a) && oak_is_string(b))
    {
      const struct oak_obj_string_t* str_a = oak_as_string(a);
      const struct oak_obj_string_t* str_b = oak_as_string(b);
      if (str_a->hash != str_b->hash)
        return 0;
      return strcmp(str_a->chars, str_b->chars) == 0;
    }
    return oak_as_obj(a) == oak_as_obj(b);
  }

  if (a.tag != b.tag)
    return 0;

  switch (a.tag)
  {
    case OAK_TAG_BOOL: return oak_as_bool(a) == oak_as_bool(b);
    case OAK_TAG_I32:  return oak_as_i32(a) == oak_as_i32(b);
    case OAK_TAG_F32:  return oak_as_f32(a) == oak_as_f32(b);
    case OAK_TAG_NONE: return 1;
    default:           return 0;
  }
}
