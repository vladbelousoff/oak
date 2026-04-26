#include "oak_value.h"

#include "oak_bind.h"
#include "oak_json_pretty2.h"
#include "oak_log.h"
#include "oak_mem.h"

#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
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
  else if (obj->type == OAK_OBJ_RECORD)
  {
    struct oak_obj_record_t* s = (struct oak_obj_record_t*)obj;
    if (s->field_name_storage)
      oak_free(s->field_name_storage, OAK_SRC_LOC);
    for (int i = 0; i < s->field_count; ++i)
      oak_value_decref(s->fields[i]);
  }
  else if (obj->type == OAK_OBJ_NATIVE_RECORD)
  {
    struct oak_obj_native_record_t* ns = (struct oak_obj_native_record_t*)obj;
    if (ns->instance && ns->type && ns->type->destroy_instance)
      ns->type->destroy_instance(ns->instance);
  }

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

struct oak_obj_record_t* oak_record_new(const int field_count,
                                        const char* const type_name,
                                        const char* const* const field_names,
                                        const usize* const field_name_len)
{
  oak_assert(field_count >= 0);
  const usize size = sizeof(struct oak_obj_record_t) +
                     (usize)field_count * sizeof(struct oak_value_t);
  struct oak_obj_record_t* s = oak_alloc(size, OAK_SRC_LOC);
  s->obj.type = OAK_OBJ_RECORD;
  oak_refcount_init(&s->obj.refcount, 1);
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
    char* const raw = oak_alloc(blob, OAK_SRC_LOC);
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
oak_obj_native_record_new(const struct oak_native_type_t* type, void* instance)
{
  struct oak_obj_native_record_t* ns =
      oak_alloc(sizeof(struct oak_obj_native_record_t), OAK_SRC_LOC);
  ns->obj.type = OAK_OBJ_NATIVE_RECORD;
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

#define OAK_JSON_MAX_DEPTH 64u

/* JSON is built with yyjson; only Oak value walking lives here. */
static yyjson_mut_val* oak_value_to_yyjson(yyjson_mut_doc* const doc,
                                           const struct oak_value_t value,
                                           unsigned depth);

/* Map key as a C string: malloc'd; free after use. */
static char* oak_map_key_cstr(const struct oak_value_t key, unsigned depth)
{
  if (depth > OAK_JSON_MAX_DEPTH)
  {
    char* t = (char*)malloc(5u);
    if (!t)
      return null;
    memcpy(t, "null", 5u);
    return t;
  }
  if (oak_is_string(key))
  {
    const struct oak_obj_string_t* s = oak_as_string(key);
    char* t = (char*)malloc(s->length + 1u);
    if (!t)
      return null;
    memcpy(t, s->chars, s->length);
    t[s->length] = '\0';
    return t;
  }
  if (oak_is_number(key) && oak_is_i32(key))
  {
    char* t = (char*)malloc(32u);
    if (!t)
      return null;
    (void)snprintf(t, 32, "%d", oak_as_i32(key));
    return t;
  }
  if (oak_is_number(key) && oak_is_f32(key))
  {
    char* t = (char*)malloc(32u);
    if (!t)
      return null;
    (void)snprintf(t, 32, "%.9g", (double)oak_as_f32(key));
    return t;
  }
  if (oak_is_bool(key))
  {
    const char* s = oak_as_bool(key) ? "true" : "false";
    const usize n = (usize)strlen(s) + 1u;
    char* t = (char*)malloc(n);
    if (!t)
      return null;
    memcpy(t, s, n);
    return t;
  }
  {
    yyjson_mut_doc* const tmp = yyjson_mut_doc_new(NULL);
    if (!tmp)
      return null;
    yyjson_mut_val* j = oak_value_to_yyjson(tmp, key, depth + 1u);
    if (!j)
    {
      yyjson_mut_doc_free(tmp);
      return null;
    }
    yyjson_mut_doc_set_root(tmp, j);
    size_t plen;
    char* out = yyjson_mut_write(tmp, 0, &plen);
    yyjson_mut_doc_free(tmp);
    return out;
  }
}

static yyjson_mut_val* oak_yyjson_str_from_oak_string(
    yyjson_mut_doc* const doc, const struct oak_obj_string_t* s)
{
  return yyjson_mut_strncpy(doc, s->chars, s->length);
}

static yyjson_mut_val* oak_yyjson_unhandled(yyjson_mut_doc* const doc,
                                            const struct oak_value_t value)
{
  if (oak_is_fn(value))
  {
    char buf[64];
    (void)snprintf(
        buf, sizeof(buf), "<fn @%zu>", (size_t)oak_as_fn(value)->code_offset);
    return yyjson_mut_strcpy(doc, buf);
  }
  if (oak_is_native_fn(value))
  {
    char tmp[256];
    const int n = oak_native_fn_format(
        tmp, (usize)sizeof(tmp), oak_as_native_fn(value));
    if (n < 0 || (usize)n >= sizeof(tmp))
      return yyjson_mut_null(doc);
    return yyjson_mut_strcpy(doc, tmp);
  }
  if (oak_is_obj(value))
  {
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "%p", (void*)oak_as_obj(value));
    return yyjson_mut_strcpy(doc, buf);
  }
  return yyjson_mut_null(doc);
}

static yyjson_mut_val* oak_value_to_yyjson(yyjson_mut_doc* const doc,
                                           const struct oak_value_t value,
                                           const unsigned depth)
{
  if (depth > OAK_JSON_MAX_DEPTH)
    return yyjson_mut_null(doc);
  if (oak_is_bool(value))
    return oak_as_bool(value) ? yyjson_mut_true(doc) : yyjson_mut_false(doc);
  if (oak_is_number(value))
  {
    if (oak_is_f32(value))
      return yyjson_mut_real(doc, (double)oak_as_f32(value));
    return yyjson_mut_sint(doc, (int64_t)oak_as_i32(value));
  }
  if (oak_is_string(value))
    return oak_yyjson_str_from_oak_string(doc, oak_as_string(value));
  if (oak_is_array(value))
  {
    yyjson_mut_val* a = yyjson_mut_arr(doc);
    if (!a)
      return null;
    const struct oak_obj_array_t* ar = oak_as_array(value);
    for (usize i = 0; i < ar->length; ++i)
    {
      yyjson_mut_val* e = oak_value_to_yyjson(doc, ar->items[i], depth + 1u);
      if (!e)
        return null;
      if (!yyjson_mut_arr_add_val(a, e))
        return null;
    }
    return a;
  }
  if (oak_is_map(value))
  {
    yyjson_mut_val* o = yyjson_mut_obj(doc);
    if (!o)
      return null;
    const struct oak_obj_map_t* m = oak_as_map(value);
    for (usize i = 0; i < m->length; ++i)
    {
      char* kc = oak_map_key_cstr(m->entries[i].key, depth);
      if (!kc)
        return null;
      yyjson_mut_val* const kj = yyjson_mut_strcpy(doc, kc);
      free(kc);
      if (!kj)
        return null;
      yyjson_mut_val* vj =
          oak_value_to_yyjson(doc, m->entries[i].value, depth + 1u);
      if (!vj)
        return null;
      if (!yyjson_mut_obj_add(o, kj, vj))
        return null;
    }
    return o;
  }
  if (oak_is_record(value))
  {
    yyjson_mut_val* o = yyjson_mut_obj(doc);
    if (!o)
      return null;
    const struct oak_obj_record_t* s = oak_as_record(value);
    for (int i = 0; i < s->field_count; ++i)
    {
      const char* key;
      char keybuf[48];
      if (s->field_name_ptrs)
        key = s->field_name_ptrs[i];
      else
      {
        (void)snprintf(keybuf, sizeof keybuf, "%d", i);
        key = keybuf;
      }
      yyjson_mut_val* fj =
          oak_value_to_yyjson(doc, s->fields[i], depth + 1u);
      if (!fj)
        return null;
      yyjson_mut_val* kjv = yyjson_mut_strcpy(doc, key);
      if (!kjv)
        return null;
      if (!yyjson_mut_obj_add(o, kjv, fj))
        return null;
    }
    return o;
  }
  if (oak_is_native_record(value))
  {
    const struct oak_obj_native_record_t* ns = oak_as_native_record(value);
    const struct oak_native_type_t* t = ns->type;
    if (!t || !ns->instance)
      return yyjson_mut_null(doc);
    yyjson_mut_val* o = yyjson_mut_obj(doc);
    if (!o)
      return null;
    {
      const struct oak_value_t self = value;
      for (int i = 0; i < t->field_count; ++i)
      {
        const struct oak_native_field_t* f = &t->fields[i];
        struct oak_value_t fv = f->getter(self);
        yyjson_mut_val* fj = oak_value_to_yyjson(doc, fv, depth + 1u);
        if (oak_is_obj(fv))
          oak_value_decref(fv);
        if (!fj)
          return null;
        if (!yyjson_mut_obj_add_val(doc, o, f->name, fj))
          return null;
      }
    }
    return o;
  }
  return oak_yyjson_unhandled(doc, value);
}

void oak_value_println(const struct oak_value_t value)
{
  if (oak_is_string(value))
  {
    fputs(oak_as_cstring(value), stdout);
    fputc('\n', stdout);
    return;
  }
  yyjson_mut_doc* const doc = yyjson_mut_doc_new(NULL);
  if (!doc)
  {
    fputs("<out of memory for JSON print>\n", stdout);
    return;
  }
  yyjson_mut_val* const root = oak_value_to_yyjson(doc, value, 0u);
  if (!root)
  {
    yyjson_mut_doc_free(doc);
    fputs("<value unprintable as JSON>\n", stdout);
    return;
  }
  yyjson_mut_doc_set_root(doc, root);
  char* p = oak_json_pretty2_write(doc);
  yyjson_mut_doc_free(doc);
  if (!p)
  {
    fputs("<JSON write failed>\n", stdout);
    return;
  }
  fputs(p, stdout);
  fputc('\n', stdout);
  free(p);
}
