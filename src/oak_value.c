#include "oak_value.h"

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
  }
  else if (obj->type == OAK_OBJ_STRUCT)
  {
    struct oak_obj_struct_t* s = (struct oak_obj_struct_t*)obj;
    for (int i = 0; i < s->field_count; ++i)
      oak_value_decref(s->fields[i]);
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

struct oak_obj_native_fn_t*
oak_native_fn_new(const oak_native_fn_t fn, const int arity, const char* name)
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

struct oak_obj_map_t* oak_map_new(void)
{
  struct oak_obj_map_t* map =
      oak_alloc(sizeof(struct oak_obj_map_t), OAK_SRC_LOC);
  map->obj.type = OAK_OBJ_MAP;
  oak_refcount_init(&map->obj.refcount, 1);
  map->length = 0;
  map->capacity = 0;
  map->entries = null;
  return map;
}

static usize map_find_index(const struct oak_obj_map_t* map,
                            const struct oak_value_t key)
{
  for (usize i = 0; i < map->length; ++i)
  {
    if (oak_value_equal(map->entries[i].key, key))
      return i;
  }
  return map->length;
}

int oak_map_get(const struct oak_obj_map_t* map,
                const struct oak_value_t key,
                struct oak_value_t* out)
{
  const usize idx = map_find_index(map, key);
  if (idx == map->length)
    return 0;
  if (out)
    *out = map->entries[idx].value;
  return 1;
}

int oak_map_has(const struct oak_obj_map_t* map, const struct oak_value_t key)
{
  return map_find_index(map, key) != map->length;
}

int oak_map_delete(struct oak_obj_map_t* map, const struct oak_value_t key)
{
  const usize idx = map_find_index(map, key);
  if (idx == map->length)
    return 0;
  oak_value_decref(map->entries[idx].key);
  oak_value_decref(map->entries[idx].value);
  /* Compact: move the last entry into this slot. Entry order is not
   * meaningful from the language's point of view. */
  const usize last = map->length - 1;
  if (idx != last)
    map->entries[idx] = map->entries[last];
  map->length = last;
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
  const usize idx = map_find_index(map, key);
  if (idx != map->length)
  {
    oak_value_incref(value);
    oak_value_decref(map->entries[idx].value);
    map->entries[idx].value = value;
    return;
  }

  if (map->length >= map->capacity)
  {
    const usize new_cap = map->capacity == 0 ? 8u : map->capacity * 2u;
    map->entries = oak_realloc(
        map->entries, new_cap * sizeof(struct oak_map_entry_t), OAK_SRC_LOC);
    map->capacity = new_cap;
  }
  oak_value_incref(key);
  oak_value_incref(value);
  map->entries[map->length].key = key;
  map->entries[map->length].value = value;
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

enum oak_fn_call_result_t oak_builtin_print(void* vm,
                                            const struct oak_value_t* args,
                                            int argc,
                                            struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_value_print(args[0]);
  *out_result = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_builtin_len(void* vm,
                                          const struct oak_value_t* args,
                                          const int argc,
                                          struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (oak_is_array(args[0]))
  {
    *out_result = OAK_VALUE_I32((int)oak_as_array(args[0])->length);
    return OAK_FN_CALL_OK;
  }
  if (oak_is_map(args[0]))
  {
    *out_result = OAK_VALUE_I32((int)oak_as_map(args[0])->length);
    return OAK_FN_CALL_OK;
  }
  if (oak_is_string(args[0]))
  {
    *out_result = OAK_VALUE_I32((int)oak_as_string(args[0])->length);
    return OAK_FN_CALL_OK;
  }
  return OAK_FN_CALL_RUNTIME_ERROR;
}

enum oak_fn_call_result_t oak_builtin_push(void* vm,
                                           const struct oak_value_t* args,
                                           const int argc,
                                           struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 2 || !oak_is_array(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_array_push(oak_as_array(args[0]), args[1]);
  *out_result = OAK_VALUE_I32((int)oak_as_array(args[0])->length);
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_builtin_has(void* vm,
                                          const struct oak_value_t* args,
                                          const int argc,
                                          struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 2 || !oak_is_map(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int found = oak_map_has(oak_as_map(args[0]), args[1]);
  *out_result = OAK_VALUE_BOOL(found);
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_builtin_delete(void* vm,
                                             const struct oak_value_t* args,
                                             const int argc,
                                             struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 2 || !oak_is_map(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int removed = oak_map_delete(oak_as_map(args[0]), args[1]);
  *out_result = OAK_VALUE_BOOL(removed);
  return OAK_FN_CALL_OK;
}

void oak_value_print(const struct oak_value_t value)
{
  if (oak_is_bool(value))
  {
    oak_log(OAK_LOG_INFO, "%s", oak_as_bool(value) ? "true" : "false");
    return;
  }
  if (oak_is_number(value))
  {
    if (oak_is_f32(value))
      oak_log(OAK_LOG_INFO, "%f", oak_as_f32(value));
    else
      oak_log(OAK_LOG_INFO, "%d", oak_as_i32(value));
    return;
  }
  if (oak_is_obj(value))
  {
    if (oak_is_string(value))
      oak_log(OAK_LOG_INFO, "%s", oak_as_cstring(value));
    else if (oak_is_fn(value))
      oak_log(OAK_LOG_INFO, "<fn @%zu>", oak_as_fn(value)->code_offset);
    else if (oak_is_native_fn(value))
    {
      char buf[160];
      oak_native_fn_format(buf, sizeof(buf), oak_as_native_fn(value));
      oak_log(OAK_LOG_INFO, "%s", buf);
    }
    else if (oak_is_array(value))
    {
      const struct oak_obj_array_t* arr = oak_as_array(value);
      oak_log(OAK_LOG_INFO, "<array len=%zu>", arr->length);
    }
    else if (oak_is_map(value))
    {
      const struct oak_obj_map_t* map = oak_as_map(value);
      oak_log(OAK_LOG_INFO, "<map len=%zu>", map->length);
    }
    else if (oak_is_struct(value))
    {
      const struct oak_obj_struct_t* s = oak_as_struct(value);
      oak_log(OAK_LOG_INFO,
              "<%s fields=%d>",
              s->type_name ? s->type_name : "struct",
              s->field_count);
    }
    else
      oak_log(OAK_LOG_INFO, "%p", oak_as_obj(value));
  }
}
