#include "oak_value_impl.h"

#include "oak_allocator.h"
#include "oak_bind.h"

#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OAK_JSON_MAX_DEPTH 64u

static yyjson_mut_val* oak_value_to_yyjson(yyjson_mut_doc* doc,
                                           oak_value_t value,
                                           unsigned depth);

static char* oak_map_key_cstr(oak_value_t key, unsigned depth)
{
  if (depth > OAK_JSON_MAX_DEPTH)
  {
    char* t = (char*)malloc(5u);
    if (!t)
      return OAK_NULL;
    memcpy(t, "null", 5u);
    return t;
  }
  if (oak_is_string(key))
  {
    const oak_obj_string_t* s = oak_as_string(key);
    char* t = (char*)malloc(s->length + 1u);
    if (!t)
      return OAK_NULL;
    memcpy(t, s->chars, s->length);
    t[s->length] = '\0';
    return t;
  }
  if (oak_is_number(key) && oak_is_i32(key))
  {
    char* t = (char*)malloc(32u);
    if (!t)
      return OAK_NULL;
    (void)snprintf(t, 32, "%d", oak_as_i32(key));
    return t;
  }
  if (oak_is_number(key) && oak_is_f32(key))
  {
    char* t = (char*)malloc(32u);
    if (!t)
      return OAK_NULL;
    (void)snprintf(t, 32, "%.9g", (double)oak_as_f32(key));
    return t;
  }
  if (oak_is_bool(key))
  {
    const char* s = oak_as_bool(key) ? "true" : "false";
    const usize n = (usize)strlen(s) + 1u;
    char* t = (char*)malloc(n);
    if (!t)
      return OAK_NULL;
    memcpy(t, s, n);
    return t;
  }
  {
    yyjson_mut_doc* const tmp = yyjson_mut_doc_new(NULL);
    if (!tmp)
      return OAK_NULL;
    yyjson_mut_val* j = oak_value_to_yyjson(tmp, key, depth + 1u);
    if (!j)
    {
      yyjson_mut_doc_free(tmp);
      return OAK_NULL;
    }
    yyjson_mut_doc_set_root(tmp, j);
    size_t plen;
    char* out = yyjson_mut_write(tmp, 0, &plen);
    yyjson_mut_doc_free(tmp);
    return out;
  }
}

static yyjson_mut_val*
yyjson_str_from_oak_string(yyjson_mut_doc* doc,
                           const oak_obj_string_t* s)
{
  return yyjson_mut_strncpy(doc, s->chars, s->length);
}

static yyjson_mut_val* yyjson_unhandled(yyjson_mut_doc* doc,
                                        oak_value_t value)
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
    const int n =
        oak_native_fn_format(tmp, (usize)sizeof(tmp), oak_as_native_fn(value));
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
  if (oak_is_native_value(value))
  {
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "<native %p>", oak_as_native_value(value));
    return yyjson_mut_strcpy(doc, buf);
  }
  return yyjson_mut_null(doc);
}

/* A weak reference is a back edge: it exists precisely because following it
 * strongly would close a cycle, so serializing its target would walk the graph
 * in circles until the depth cap cut it off. Print the identity of the target
 * instead — enough to tell two back edges apart, and never recursive. */
static yyjson_mut_val* yyjson_weak_ref(yyjson_mut_doc* doc, oak_value_t value)
{
  void* const target = (void*)oak_value_obj_resolve(value);
  char buf[96];
  if (oak_is_record(value))
  {
    const oak_obj_record_t* const s = oak_as_record(value);
    (void)snprintf(buf,
                   sizeof(buf),
                   "<weak %s @%p>",
                   s->type_name ? s->type_name : "record",
                   target);
  }
  else
    (void)snprintf(buf, sizeof(buf), "<weak @%p>", target);
  return yyjson_mut_strcpy(doc, buf);
}

static yyjson_mut_val* oak_value_to_yyjson(yyjson_mut_doc* doc,
                                           oak_value_t value,
                                           unsigned depth)
{
  if (depth > OAK_JSON_MAX_DEPTH)
    return yyjson_mut_null(doc);
  if (oak_is_none_like(value))
    return yyjson_mut_null(doc);
  if (oak_is_weak_obj(value))
    return yyjson_weak_ref(doc, value);
  if (oak_is_bool(value))
    return oak_as_bool(value) ? yyjson_mut_true(doc) : yyjson_mut_false(doc);
  if (oak_is_number(value))
  {
    if (oak_is_f32(value))
      return yyjson_mut_real(doc, (double)oak_as_f32(value));
    return yyjson_mut_sint(doc, (int64_t)oak_as_i32(value));
  }
  if (oak_is_string(value))
    return yyjson_str_from_oak_string(doc, oak_as_string(value));
  if (oak_is_array(value))
  {
    yyjson_mut_val* a = yyjson_mut_arr(doc);
    if (!a)
      return OAK_NULL;
    const oak_obj_array_t* ar = oak_as_array(value);
    for (usize i = 0; i < ar->length; ++i)
    {
      yyjson_mut_val* e = oak_value_to_yyjson(doc, ar->items[i], depth + 1u);
      if (!e)
        return OAK_NULL;
      if (!yyjson_mut_arr_add_val(a, e))
        return OAK_NULL;
    }
    return a;
  }
  if (oak_is_map(value))
  {
    yyjson_mut_val* o = yyjson_mut_obj(doc);
    if (!o)
      return OAK_NULL;
    const oak_obj_map_t* m = oak_as_map(value);
    for (usize i = 0; i < m->length; ++i)
    {
      char* kc = oak_map_key_cstr(m->entries[i].key, depth);
      if (!kc)
        return OAK_NULL;
      yyjson_mut_val* const kj = yyjson_mut_strcpy(doc, kc);
      free(kc);
      if (!kj)
        return OAK_NULL;
      yyjson_mut_val* vj =
          oak_value_to_yyjson(doc, m->entries[i].value, depth + 1u);
      if (!vj)
        return OAK_NULL;
      if (!yyjson_mut_obj_add(o, kj, vj))
        return OAK_NULL;
    }
    return o;
  }
  if (oak_is_record(value))
  {
    yyjson_mut_val* o = yyjson_mut_obj(doc);
    if (!o)
      return OAK_NULL;
    const oak_obj_record_t* s = oak_as_record(value);
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
      yyjson_mut_val* fj = oak_value_to_yyjson(doc, s->fields[i], depth + 1u);
      if (!fj)
        return OAK_NULL;
      yyjson_mut_val* kjv = yyjson_mut_strcpy(doc, key);
      if (!kjv)
        return OAK_NULL;
      if (!yyjson_mut_obj_add(o, kjv, fj))
        return OAK_NULL;
    }
    return o;
  }
  if (oak_is_native_record(value))
  {
    const oak_obj_native_record_t* ns = oak_as_native_record(value);
    const oak_bind_type_t* t = ns->type;
    if (!t || !ns->instance)
      return yyjson_mut_null(doc);
    yyjson_mut_val* o = yyjson_mut_obj(doc);
    if (!o)
      return OAK_NULL;
    {
      const oak_value_t self = value;
      const oak_bind_field_t* type_fields =
          OAK_CDATA(oak_bind_field_t, t->fields);
      for (usize i = 0; i < oak_size(t->fields); ++i)
      {
        const oak_bind_field_t* f = &type_fields[i];
        oak_value_t fv = f->getter(self, f->user_data);
        yyjson_mut_val* fj = oak_value_to_yyjson(doc, fv, depth + 1u);
        oak_value_decref(fv);
        if (!fj)
          return OAK_NULL;
        if (!yyjson_mut_obj_add_val(doc, o, f->name, fj))
          return OAK_NULL;
      }
    }
    return o;
  }
  return yyjson_unhandled(doc, value);
}

oak_obj_string_t*
oak_value_to_string_in_table(oak_allocator_t* allocator,
                             const u32 table_id,
                             oak_value_t value)
{
  if (oak_is_none_like(value))
    return oak_string_new_len_in_table(allocator, table_id, "none", 4);
  if (oak_is_bool(value))
  {
    const char* s = oak_as_bool(value) ? "true" : "false";
    return oak_string_new_len_in_table(allocator, table_id, s, strlen(s));
  }
  if (oak_is_number(value))
  {
    char buf[64];
    int n;
    if (oak_is_f32(value))
      n = snprintf(buf, sizeof(buf), "%g", (double)oak_as_f32(value));
    else
      n = snprintf(buf, sizeof(buf), "%d", oak_as_i32(value));
    if (n < 0)
      return OAK_NULL;
    return oak_string_new_len_in_table(allocator, table_id, buf, (usize)n);
  }
  if (oak_is_string(value))
  {
    oak_obj_string_t* string = oak_as_string(value);
    if (string->obj.table_id == table_id)
    {
      oak_obj_incref(&string->obj);
      return string;
    }
    return oak_string_new_len_in_table(
        allocator, table_id, string->chars, string->length);
  }
  if (oak_is_native_value(value))
    return oak_string_from_value_repr_in_table(allocator, table_id, value);
  if (oak_is_handle(value))
  {
    char buf[32];
    const int n = snprintf(buf,
                           sizeof(buf),
                           "%llu",
                           (unsigned long long)oak_value_as_handle(value));
    if (n < 0)
      return OAK_NULL;
    return oak_string_new_len_in_table(allocator, table_id, buf, (usize)n);
  }
  yyjson_mut_doc* const doc = yyjson_mut_doc_new(NULL);
  if (!doc)
    return OAK_NULL;
  yyjson_mut_val* const root = oak_value_to_yyjson(doc, value, 0u);
  if (!root)
  {
    yyjson_mut_doc_free(doc);
    return OAK_NULL;
  }
  yyjson_mut_doc_set_root(doc, root);
  size_t json_len;
  char* p = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, &json_len);
  yyjson_mut_doc_free(doc);
  if (!p)
    return OAK_NULL;
  oak_obj_string_t* s =
      oak_string_new_len_in_table(allocator, table_id, p, json_len);
  free(p);
  return s;
}

oak_obj_string_t* oak_value_to_string(oak_allocator_t* allocator,
                                             oak_value_t value)
{
  return oak_value_to_string_in_table(allocator, 0u, value);
}
