#include "oak_value_impl.h"

#include "oak_bind.h"

#include <stdio.h>

int oak_value_snprint_repr(char* buf, usize size, oak_value_t value)
{
  if (oak_is_none_like(value))
    return snprintf(buf, size, "none");
  if (oak_is_bool(value))
    return snprintf(buf, size, "%s", oak_as_bool(value) ? "true" : "false");
  if (oak_is_number(value))
  {
    if (oak_is_f32(value))
      return snprintf(buf, size, "%g", (double)oak_as_f32(value));
    return snprintf(buf, size, "%d", oak_as_i32(value));
  }
  if (oak_is_obj(value))
  {
    if (oak_is_string(value))
      return snprintf(buf, size, "%s", oak_as_cstring(value));
    if (oak_is_fn(value))
      return snprintf(buf, size, "<fn @%zu>", oak_as_fn(value)->code_offset);
    if (oak_is_native_fn(value))
      return oak_native_fn_format(buf, size, oak_as_native_fn(value));
    if (oak_is_array(value))
      return snprintf(
          buf, size, "<array len=%zu>", oak_as_array(value)->length);
    if (oak_is_map(value))
      return snprintf(buf, size, "<map len=%zu>", oak_as_map(value)->length);
    if (oak_is_record(value))
    {
      const oak_obj_record_t* s = oak_as_record(value);
      return snprintf(buf,
                      size,
                      "<%s fields=%d>",
                      s->type_name ? s->type_name : "record",
                      s->field_count);
    }
    if (oak_is_native_record(value))
    {
      const oak_obj_native_record_t* ns = oak_as_native_record(value);
      const oak_bind_type_t* t = ns->type;
      const char* nm = (t && t->name) ? t->name : "native";
      return snprintf(buf, size, "<%s>", nm);
    }
    return snprintf(buf, size, "%p", (void*)oak_as_obj(value));
  }
  if (oak_is_native_value(value))
    return snprintf(buf, size, "<native %p>", oak_as_native_value(value));
  if (size > 0)
    buf[0] = '\0';
  return 0;
}

oak_obj_string_t*
oak_string_from_value_repr_in_table(oak_allocator_t* allocator,
                                    const u32 table_id,
                                    oak_value_t value)
{
  char buf[4096];
  const int n = oak_value_snprint_repr(buf, sizeof(buf), value);
  if (n < 0)
    return null;
  usize len = (usize)n;
  if (len >= sizeof(buf))
    len = sizeof(buf) - 1u;
  return oak_string_new_len_in_table(allocator, table_id, buf, len);
}

oak_obj_string_t*
oak_string_from_value_repr(oak_allocator_t* allocator,
                           oak_value_t value)
{
  return oak_string_from_value_repr_in_table(allocator, 0u, value);
}

/* Buffering is left to stdio: a terminal is line-buffered and a pipe is fully
 * buffered, same as any other program. A debug session needs each line to
 * reach the adapter as it happens, so oak_debugger_init() turns the buffering
 * off for the process rather than this path paying for a flush per print. */
void oak_value_println(oak_allocator_t* allocator,
                       oak_value_t value)
{
  oak_obj_string_t* s = oak_value_to_string(allocator, value);
  if (!s)
    return;
  fputs(s->chars, stdout);
  fputc('\n', stdout);
  oak_obj_decref(&s->obj);
}
