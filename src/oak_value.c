#include "oak_value.h"

#include "oak_log.h"
#include "oak_mem.h"

#include <string.h>

void oak_obj_incref(oak_obj_t* obj)
{
  oak_refcount_inc(&obj->refcount);
}

void oak_obj_decref(oak_obj_t* obj)
{
  if (oak_refcount_dec(&obj->refcount))
    oak_free(obj, OAK_SRC_LOC);
}

static uint32_t hash_string(const char* chars, const size_t length)
{
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < length; ++i)
  {
    hash ^= (uint8_t)chars[i];
    hash *= 16777619u;
  }
  return hash;
}

oak_obj_string_t* oak_make_string(const char* chars, const size_t length)
{
  oak_obj_string_t* str =
      oak_alloc(sizeof(oak_obj_string_t) + length + 1, OAK_SRC_LOC);
  str->obj.type = OAK_OBJ_STRING;
  oak_refcount_init(&str->obj.refcount, 1);
  str->length = length;
  memcpy(str->chars, chars, length);
  str->chars[length] = 0;
  str->hash = hash_string(chars, length);
  return str;
}

oak_obj_string_t* oak_string_concat(const oak_obj_string_t* a,
                                    const oak_obj_string_t* b)
{
  const size_t length = a->length + b->length;
  oak_obj_string_t* str =
      oak_alloc(sizeof(oak_obj_string_t) + length + 1, OAK_SRC_LOC);
  str->obj.type = OAK_OBJ_STRING;
  oak_refcount_init(&str->obj.refcount, 1);
  str->length = length;
  memcpy(str->chars, a->chars, a->length);
  memcpy(str->chars + a->length, b->chars, b->length);
  str->chars[length] = 0;
  str->hash = hash_string(str->chars, length);
  return str;
}

int oak_is_truthy(const oak_value_t value)
{
  switch (value.type)
  {
    case OAK_VAL_BOOL:
      return oak_as_bool(value);
    case OAK_VAL_NUMBER:
      if (oak_is_f32(value))
        return oak_as_f32(value) != 0.0f;
      return oak_as_i32(value) != 0;
    case OAK_VAL_OBJ:
      return 1;
  }

  return 0;
}

int oak_value_equal(const oak_value_t a, const oak_value_t b)
{
  if (a.type != b.type)
    return 0;

  switch (a.type)
  {
    case OAK_VAL_BOOL:
      return oak_as_bool(a) == oak_as_bool(b);
    case OAK_VAL_NUMBER:
      if (a.as.number.flags != b.as.number.flags)
        return 0;
      // it works for floats too
      return a.as.number.integer == b.as.number.integer;
    case OAK_VAL_OBJ:
      if (oak_is_string(a) && oak_is_string(b))
      {
        const oak_obj_string_t* str_a = oak_as_string(a);
        const oak_obj_string_t* str_b = oak_as_string(b);
        if (str_a->length != str_b->length)
          return 0;
        if (str_a->hash != str_b->hash)
          return 0;
        return memcmp(str_a->chars, str_b->chars, str_a->length) == 0;
      }
      return oak_as_obj(a) == oak_as_obj(b);
  }

  return 0;
}

void oak_value_incref(const oak_value_t value)
{
  if (oak_is_obj(value))
    oak_obj_incref(oak_as_obj(value));
}

void oak_value_decref(const oak_value_t value)
{
  if (oak_is_obj(value))
    oak_obj_decref(oak_as_obj(value));
}

void oak_value_print(const oak_value_t value)
{
  switch (value.type)
  {
    case OAK_VAL_BOOL:
      oak_log(OAK_LOG_INF, "%s", oak_as_bool(value) ? "true" : "false");
      break;
    case OAK_VAL_NUMBER:
      if (oak_is_f32(value))
        oak_log(OAK_LOG_INF, "%f", oak_as_f32(value));
      else
        oak_log(OAK_LOG_INF, "%d", oak_as_i32(value));
      break;
    case OAK_VAL_OBJ:
      if (oak_is_string(value))
        oak_log(OAK_LOG_INF, "%s", oak_as_cstring(value));
      else
        oak_log(OAK_LOG_INF, "%p", oak_as_obj(value));
      break;
  }
}
