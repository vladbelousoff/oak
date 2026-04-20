#include "oak_vm_internal.h"

#include <stdarg.h>
#include <stdio.h>

const char* oak_vm_value_kind_desc(const struct oak_value_t v)
{
  if (oak_is_bool(v))
    return "bool";
  if (oak_is_number(v))
    return oak_is_f32(v) ? "float" : "integer";
  if (oak_is_string(v))
    return "string";
  if (oak_is_fn(v))
    return "function";
  if (oak_is_native_fn(v))
    return "native function";
  if (oak_is_array(v))
    return "array";
  if (oak_is_map(v))
    return "map";
  if (oak_is_obj(v))
    return "object";
  return "value";
}

void oak_vm_runtime_error(const struct oak_vm_t* vm, const char* fmt, ...)
{
  static _Thread_local char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  const usize offset = (usize)(vm->ip - vm->chunk->bytecode - 1);
  oak_assert(vm->chunk->locations != null);
  const struct oak_code_loc_t loc = vm->chunk->locations[offset];
  int col = loc.column;
  if (col < 1)
    col = 1;

  oak_log(OAK_LOG_ERROR, "%d:%d: error: %s", loc.line, col, buf);
}
