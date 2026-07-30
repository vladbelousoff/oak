#include "internal/oak_vm.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

const char* oak_vm_value_kind_desc(const struct oak_value_t v)
{
  if (oak_is_none_like(v))
    return "none";
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
  if (oak_is_interface_object(v))
    return "interface object";
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

  const struct oak_chunk_t* chunk = vm->chunk;
  if (!chunk || !chunk->debug || !chunk->debug->locations)
  {
    oak_log(OAK_LOG_ERROR, "error: %s", buf);
    return;
  }

  /* During host calls (oak_vm_call) the ip points at a static halt
   * trampoline outside this chunk; mapping it to a source location would
   * index the locations array out of bounds. */
  const uintptr_t ip = (uintptr_t)vm->ip;
  const uintptr_t code = (uintptr_t)chunk->bytecode;
  if (ip <= code || ip > code + chunk->count)
  {
    oak_log(OAK_LOG_ERROR, "error: %s", buf);
    return;
  }

  const usize offset = (usize)(ip - code) - 1u;
  const struct oak_code_loc_t loc = chunk->debug->locations[offset];
  int col = loc.column;
  if (col < 1)
    col = 1;

  oak_log(OAK_LOG_ERROR, "%d:%d: error: %s", loc.line, col, buf);
}

void oak_vm_report_stack_overflow(const struct oak_vm_t* vm)
{
  oak_vm_runtime_error(vm, "stack overflow (max %d values)", OAK_STACK_MAX);
}
