#include "internal/oak_vm.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

const char* oak_vm_value_kind_desc(const oak_value_t v)
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

void oak_vm_clear_last_error(oak_vm_t* vm)
{
  vm->last_error.line = 0;
  vm->last_error.column = 0;
  vm->last_error.message[0] = '\0';
}

/* Records the error on the VM as well as logging it, so that an embedder can
 * recover the message and location through oak_vm_last_error instead of
 * scraping stderr. The VM pointer is const at every call site (72 of them) and
 * the error slot is a pure diagnostic sink -- it is not part of execution
 * state -- so the constness is cast away here rather than churning them all. */
static void record(const oak_vm_t* vm,
                   const int line,
                   const int column,
                   const char* message)
{
  oak_vm_t* mutable_vm = (oak_vm_t*)vm;
  oak_diagnostic_t* slot = &mutable_vm->last_error;
  slot->line = line;
  slot->column = column;
  snprintf(slot->message, sizeof(slot->message), "%s", message);
  ++mutable_vm->error_seq;
}

void oak_vm_runtime_error(const oak_vm_t* vm, const char* fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  const oak_chunk_t* chunk = vm->chunk;
  if (!chunk || !chunk->debug || !chunk->debug->locations)
  {
    record(vm, 0, 0, buf);
    oak_log(OAK_LOG_ERROR, "error: %s", buf);
    return;
  }

  /* During host calls (oak_vm_call) the ip points at a static halt
   * trampoline outside this chunk; mapping it to a source location would
   * index the locations array out of bounds. */
  const uintptr_t ip = (uintptr_t)vm->ip;
  const uintptr_t code = (uintptr_t)oak_chunk_code(chunk);
  if (ip <= code || ip > code + oak_chunk_size(chunk))
  {
    record(vm, 0, 0, buf);
    oak_log(OAK_LOG_ERROR, "error: %s", buf);
    return;
  }

  const usize offset = (usize)(ip - code) - 1u;
  const oak_code_loc_t loc = oak_chunk_loc(chunk, offset);
  int col = loc.column;
  if (col < 1)
    col = 1;

  record(vm, loc.line, col, buf);
  oak_log(OAK_LOG_ERROR, "%d:%d: error: %s", loc.line, col, buf);
}

void oak_vm_report_stack_overflow(const oak_vm_t* vm)
{
  oak_vm_runtime_error(vm, "stack overflow (max %d values)", OAK_STACK_MAX);
}
