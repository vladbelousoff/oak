#include "oak_debugger.h"

#include "oak_vector.h"
#include "oak_dap.h"

#include "oak_chunk_impl.h"
#include "oak_log.h"
#include "oak_value_impl.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ip_in_chunk(const u8* ip, const oak_chunk_t* chunk)
{
  const uintptr_t addr = (uintptr_t)ip;
  const uintptr_t base = (uintptr_t)oak_chunk_code(chunk);
  return addr >= base && addr < base + oak_chunk_size(chunk);
}

/* Parse a strictly positive decimal integer, rejecting trailing garbage such
 * as "12xyz" that atoi() would silently accept. Returns 1 on success. */
static int parse_positive_int(const char* arg, int* out)
{
  while (*arg == ' ')
    ++arg;
  if (*arg == '\0')
    return 0;
  char* end = OAK_NULL;
  const long value = strtol(arg, &end, 10);
  while (*end == ' ')
    ++end;
  if (*end != '\0' || value <= 0 || value > INT_MAX)
    return 0;
  *out = (int)value;
  return 1;
}

/* Source names are borrowed from chunk debug info and are not guaranteed to be
 * interned, so compare by value (with a pointer fast-path). */
static char source_path_char(const char c)
{
#ifdef _WIN32
  if (c == '\\')
    return '/';
  if (c >= 'A' && c <= 'Z')
    return (char)(c - 'A' + 'a');
#endif
  return c;
}

static int source_eq(const char* a, const char* b)
{
  if (a == b)
    return 1;
  if (!a || !b)
    return 0;
  while (*a && *b)
  {
    if (source_path_char(*a) != source_path_char(*b))
      return 0;
    ++a;
    ++b;
  }
  return *a == *b;
}

void oak_debugger_init(oak_debugger_t* dbg,
                       oak_allocator_t* allocator)
{
  memset(dbg, 0, sizeof(*dbg));
  dbg->allocator = allocator;
  dbg->breakpoints = oak_vector_new(allocator, sizeof(oak_breakpoint_t));
  dbg->line_offsets = oak_vector_new(allocator, sizeof(int));
  OAK_ASSERT(dbg->breakpoints && dbg->line_offsets);
  dbg->mode = OAK_DEBUG_MODE_RUN;
  dbg->next_bp_id = 1;
  dbg->initial_break = 1;
  dbg->in = stdin;
  dbg->out = stdout;
  /* A debug adapter captures the program over a pipe, which stdio would
   * buffer fully -- everything print() emitted would reach the client in one
   * blob at exit rather than at the stop it belongs to. Unbuffer for the
   * session; a normal run keeps stdio's own policy. _IOLBF is not an option:
   * MSVC accepts it and silently buffers fully anyway. */
  setvbuf(stdout, OAK_NULL, _IONBF, 0);
}

void oak_debugger_free(oak_debugger_t* dbg)
{
  const oak_breakpoint_t* const bps =
      OAK_CDATA(oak_breakpoint_t, dbg->breakpoints);
  for (usize i = 0; i < oak_size(dbg->breakpoints); ++i)
    oak_free(dbg->allocator, (void*)bps[i].source_name, OAK_HERE);
  oak_destroy(dbg->breakpoints);
  oak_destroy(dbg->line_offsets);
  if (dbg->source_map.data)
    oak_file_unmap(&dbg->source_map);
  if (dbg->cached_source_path)
    oak_free(dbg->allocator, (void*)dbg->cached_source_path, OAK_HERE);
  memset(dbg, 0, sizeof(*dbg));
}

int oak_debugger_add_breakpoint(oak_debugger_t* dbg, const int line,
                                const char* source_name)
{
  char* owned_name = OAK_NULL;
  if (source_name)
  {
    const usize len = strlen(source_name);
    owned_name = oak_alloc(dbg->allocator, len + 1, OAK_HERE);
    if (!owned_name)
      return -1;
    memcpy(owned_name, source_name, len + 1);
  }
  oak_breakpoint_t bp;
  bp.id = dbg->next_bp_id++;
  bp.line = line;
  bp.source_name = owned_name;
  bp.enabled = 1;
  if (!oak_push_back(dbg->breakpoints, &bp))
  {
    oak_free(dbg->allocator, owned_name, OAK_HERE);
    return -1;
  }
  return bp.id;
}

int oak_debugger_remove_breakpoint(oak_debugger_t* dbg, const int id)
{
  const oak_breakpoint_t* const bps =
      OAK_CDATA(oak_breakpoint_t, dbg->breakpoints);
  for (usize i = 0; i < oak_size(dbg->breakpoints); ++i)
  {
    if (bps[i].id == id)
    {
      oak_free(dbg->allocator, (void*)bps[i].source_name, OAK_HERE);
      oak_erase(dbg->breakpoints, i);
      return 1;
    }
  }
  return 0;
}

void oak_debugger_clear_breakpoints(oak_debugger_t* dbg,
                                    const char* source_name)
{
  /* Counts down so the erase inside remove_breakpoint cannot skip an entry. */
  for (usize i = oak_size(dbg->breakpoints); i > 0; --i)
  {
    const oak_breakpoint_t* const bp = oak_cget(dbg->breakpoints, i - 1);
    if (!source_name || source_eq(bp->source_name, source_name))
      oak_debugger_remove_breakpoint(dbg, bp->id);
  }
}

static int hit_breakpoint(const oak_debugger_t* dbg, const int line,
                          const char* source_name)
{
  const oak_breakpoint_t* const bps =
      OAK_CDATA(oak_breakpoint_t, dbg->breakpoints);
  for (usize i = 0; i < oak_size(dbg->breakpoints); ++i)
  {
    const oak_breakpoint_t* bp = &bps[i];
    if (!bp->enabled || bp->line != line)
      continue;
    if (bp->source_name)
    {
      if (!source_eq(bp->source_name, source_name))
        continue;
    }
    return 1;
  }
  return 0;
}

/* ── source file cache ─────────────────────────────────────────────── */

static void cache_source(oak_debugger_t* dbg, const char* path)
{
  if (!path)
    return;
  if (dbg->cached_source_path && strcmp(dbg->cached_source_path, path) == 0)
    return;

  if (dbg->source_map.data)
  {
    oak_file_unmap(&dbg->source_map);
    dbg->source_map.data = OAK_NULL;
    dbg->source_map.size = 0;
  }
  oak_clear(dbg->line_offsets);
  if (dbg->cached_source_path)
  {
    oak_free(dbg->allocator, (void*)dbg->cached_source_path, OAK_HERE);
    dbg->cached_source_path = OAK_NULL;
  }

  if (oak_file_map(path, &dbg->source_map) != 0)
    return;

  /* Own a copy of the path: the borrowed chunk source name is not guaranteed
   * to outlive repeated cache switches. */
  const usize path_len = strlen(path);
  char* owned_path = oak_alloc(dbg->allocator, path_len + 1, OAK_HERE);
  if (owned_path)
  {
    memcpy(owned_path, path, path_len + 1);
    dbg->cached_source_path = owned_path;
  }

  const char* data = dbg->source_map.data;
  const usize size = dbg->source_map.size;

  const int zero = 0;
  OAK_ASSERT(oak_push_back(dbg->line_offsets, &zero));
  for (usize i = 0; i < size; ++i)
  {
    if (data[i] == '\n')
    {
      const int offset = (int)(i + 1);
      OAK_ASSERT(oak_push_back(dbg->line_offsets, &offset));
    }
  }
}

static void print_source_line(const oak_debugger_t* dbg,
                              const int line,
                              const int current)
{
  FILE* const out = dbg->out;
  const int line_count = (int)oak_size(dbg->line_offsets);
  if (!dbg->source_map.data || line < 1 || line > line_count)
    return;
  const int* const offsets = OAK_CDATA(int, dbg->line_offsets);
  const int off = offsets[line - 1];
  const char* data = dbg->source_map.data;
  const usize size = dbg->source_map.size;
  int end = (line < line_count) ? offsets[line] : (int)size;
  while (end > off && (data[end - 1] == '\n' || data[end - 1] == '\r'))
    --end;
  const char* marker = (line == current) ? ">" : " ";
  fprintf(out, "%s %4d  %.*s\n", marker, line, end - off, data + off);
}

/* ── inspection commands ───────────────────────────────────────────── */

static void cmd_locals(const oak_vm_t* vm, FILE* out)
{
  const oak_chunk_t* chunk = vm->chunk;
  if (!chunk->debug)
  {
    fprintf(out, "(no debug info)\n");
    return;
  }
  const usize offset = (usize)(vm->ip - oak_chunk_code(chunk));
  const oak_chunk_debug_t* d = chunk->debug;
  int printed[OAK_STACK_MAX] = { 0 };
  int found = 0;

  for (usize i = oak_size(d->debug_locals); i > 0; --i)
  {
    const oak_debug_local_t* dl = (const oak_debug_local_t*)oak_cget(d->debug_locals, i - 1);
    if (dl->slot < 0 || dl->slot >= OAK_STACK_MAX)
      continue;
    if (dl->offset > offset)
      continue;
    if (dl->end_offset != (usize)-1 && offset >= dl->end_offset)
      continue;
    if (printed[dl->slot])
      continue;
    printed[dl->slot] = 1;

    const usize idx = vm->stack_base + (usize)dl->slot;
    if (idx >= (usize)(vm->sp - vm->stack))
      continue;

    char buf[256];
    oak_value_snprint_repr(buf, sizeof(buf), vm->stack[idx]);
    fprintf(out, "  %s = %s\n", dl->name, buf);
    found = 1;
  }
  if (!found)
    fprintf(out, "  (no locals in scope)\n");
}

static void cmd_print(const oak_vm_t* vm, const char* name, FILE* out)
{
  const oak_chunk_t* chunk = vm->chunk;
  if (!chunk->debug)
  {
    fprintf(out, "(no debug info)\n");
    return;
  }
  const usize offset = (usize)(vm->ip - oak_chunk_code(chunk));
  const oak_chunk_debug_t* d = chunk->debug;

  for (usize i = oak_size(d->debug_locals); i > 0; --i)
  {
    const oak_debug_local_t* dl = (const oak_debug_local_t*)oak_cget(d->debug_locals, i - 1);
    if (dl->slot < 0 || dl->slot >= OAK_STACK_MAX)
      continue;
    if (dl->offset > offset)
      continue;
    if (dl->end_offset != (usize)-1 && offset >= dl->end_offset)
      continue;
    if (strcmp(dl->name, name) != 0)
      continue;
    const usize idx = vm->stack_base + (usize)dl->slot;
    if (idx >= (usize)(vm->sp - vm->stack))
    {
      fprintf(out, "%s = (not yet initialized)\n", name);
      return;
    }
    char buf[256];
    oak_value_snprint_repr(buf, sizeof(buf), vm->stack[idx]);
    fprintf(out, "%s = %s\n", name, buf);
    return;
  }
  fprintf(out, "no local named '%s' in scope\n", name);
}

static void cmd_backtrace(const oak_vm_t* vm, FILE* out)
{
  const oak_chunk_t* chunk = vm->chunk;
  int line = 0;
  if (chunk->debug)
  {
    const usize off = (usize)(vm->ip - oak_chunk_code(chunk));
    if (off < oak_chunk_size(chunk))
      line = oak_chunk_loc(chunk, off).line;
  }
  const char* src = chunk->debug ? chunk->debug->source_name : OAK_NULL;
  fprintf(out, "#0  <current> at %s:%d\n", src ? src : "?", line);

  for (int i = vm->frame_count - 1; i >= 0; --i)
  {
    const oak_call_frame_t* fr = &vm->frames[i];
    const char* name = "<unknown>";
    if (fr->fn_slot < OAK_STACK_MAX)
    {
      const oak_value_t fn_val = vm->stack[fr->fn_slot];
      if (oak_is_fn(fn_val))
      {
        const oak_obj_fn_t* fn = oak_as_fn(fn_val);
        if (fn->name)
          name = fn->name;
        else
          name = "<anonymous>";
      }
      else if (oak_is_native_fn(fn_val))
      {
        const oak_obj_native_fn_t* nf = oak_as_native_fn(fn_val);
        if (nf->name)
          name = nf->name;
        else
          name = "<native>";
      }
    }
    int ret_line = 0;
    const char* ret_src = OAK_NULL;
    if (fr->return_chunk && fr->return_chunk->debug &&
        ip_in_chunk(fr->return_ip, fr->return_chunk))
    {
      const usize ret_off =
          (usize)(fr->return_ip - oak_chunk_code(fr->return_chunk));
      if (ret_off > 0)
        ret_line = oak_chunk_loc(fr->return_chunk, ret_off - 1).line;
      ret_src = fr->return_chunk->debug->source_name;
    }
    fprintf(out,
            "#%d  %s at %s:%d\n",
            vm->frame_count - i,
            name,
            ret_src ? ret_src : "?",
            ret_line);
  }
}

static void cmd_stack(const oak_vm_t* vm, FILE* out)
{
  const int depth = (int)(vm->sp - vm->stack);
  if (depth == 0)
  {
    fprintf(out, "  (empty stack)\n");
    return;
  }
  for (int i = depth - 1; i >= 0; --i)
  {
    char buf[256];
    oak_value_snprint_repr(buf, sizeof(buf), vm->stack[i]);
    const char* marker = ((usize)i == vm->stack_base) ? " <- base" : "";
    fprintf(out, "  [%3d] %s%s\n", i, buf, marker);
  }
}

static void cmd_list(oak_debugger_t* dbg,
                     const oak_vm_t* vm)
{
  FILE* const out = dbg->out;
  const oak_chunk_t* chunk = vm->chunk;
  if (!chunk->debug || !chunk->debug->source_name)
  {
    fprintf(out, "(no source info)\n");
    return;
  }
  cache_source(dbg, chunk->debug->source_name);
  if (!dbg->source_map.data)
  {
    fprintf(out, "(could not load source '%s')\n",
            chunk->debug->source_name);
    return;
  }
  const usize off = (usize)(vm->ip - oak_chunk_code(chunk));
  const int current = oak_chunk_loc(chunk, off).line;
  const int start = current - 5 < 1 ? 1 : current - 5;
  const int line_count = (int)oak_size(dbg->line_offsets);
  const int end = current + 5 > line_count ? line_count
                                                 : current + 5;
  for (int i = start; i <= end; ++i)
    print_source_line(dbg, i, current);
}

static void cmd_disassemble(const oak_vm_t* vm, FILE* out)
{
  const oak_chunk_t* chunk = vm->chunk;
  const usize current = (usize)(vm->ip - oak_chunk_code(chunk));
  usize start = current > 20 ? current - 20 : 0;
  if (start > 0)
  {
    start = 0;
    for (usize scan = 0; scan < oak_chunk_size(chunk) && scan < current;)
    {
      const usize next = oak_chunk_disassemble_instruction(chunk, scan);
      if (next > current - 20)
      {
        start = scan;
        break;
      }
      scan = next;
    }
  }
  usize end = current + 30;
  if (end > oak_chunk_size(chunk))
    end = oak_chunk_size(chunk);

  fprintf(out, "--- bytecode around offset %04zu ---\n", current);
  for (usize off = start; off < end;)
  {
    if (off == current)
      fprintf(out, "==> ");
    else
      fprintf(out, "    ");
    off = oak_chunk_disassemble_instruction(chunk, off);
  }
}

static void cmd_breakpoints(const oak_debugger_t* dbg)
{
  FILE* const out = dbg->out;
  if (oak_size(dbg->breakpoints) == 0)
  {
    fprintf(out, "no breakpoints\n");
    return;
  }
  const oak_breakpoint_t* const bps =
      OAK_CDATA(oak_breakpoint_t, dbg->breakpoints);
  for (usize i = 0; i < oak_size(dbg->breakpoints); ++i)
  {
    const oak_breakpoint_t* bp = &bps[i];
    fprintf(out,
            "  #%d  %s:%d  %s\n",
            bp->id,
            bp->source_name ? bp->source_name : "*",
            bp->line,
            bp->enabled ? "enabled" : "disabled");
  }
}

static void cmd_help(FILE* out)
{
  fprintf(out,
    "Commands:\n"
    "  continue (c)       Resume execution\n"
    "  step (s)           Step to next source line (into calls)\n"
    "  next (n)           Step to next source line (over calls)\n"
    "  finish (f)         Run until current function returns\n"
    "  break <line> (b)   Set breakpoint at source line\n"
    "  delete <id> (d)    Remove breakpoint by id\n"
    "  breakpoints        List all breakpoints\n"
    "  print <var> (p)    Print local variable\n"
    "  locals             Print all locals in scope\n"
    "  backtrace (bt)     Print call stack\n"
    "  stack              Print raw VM stack\n"
    "  list (l)           Show source around current line\n"
    "  disassemble (dis)  Show bytecode around current IP\n"
    "  help (h)           Show this help\n"
    "  quit (q)           Stop execution\n");
}

/* ── command REPL ──────────────────────────────────────────────────── */

static void debugger_repl(oak_debugger_t* dbg,
                          const oak_vm_t* vm,
                          const int current_line,
                          const char* current_source)
{
  FILE* const out = dbg->out;
  char buf[512];
  for (;;)
  {
    fprintf(out, "(oak-dbg) ");
    fflush(out);
    if (!fgets(buf, (int)sizeof(buf), dbg->in))
    {
      dbg->quit_requested = 1;
      return;
    }
    usize len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';
    if (len == 0)
      continue;

    if (strcmp(buf, "continue") == 0 || strcmp(buf, "c") == 0)
    {
      dbg->mode = OAK_DEBUG_MODE_RUN;
      return;
    }
    if (strcmp(buf, "step") == 0 || strcmp(buf, "s") == 0)
    {
      dbg->mode = OAK_DEBUG_MODE_STEP;
      dbg->step_line = current_line;
      dbg->step_source = current_source;
      dbg->step_frame_count = vm->frame_count;
      return;
    }
    if (strcmp(buf, "next") == 0 || strcmp(buf, "n") == 0)
    {
      dbg->mode = OAK_DEBUG_MODE_NEXT;
      dbg->step_line = current_line;
      dbg->step_source = current_source;
      dbg->step_frame_count = vm->frame_count;
      return;
    }
    if (strcmp(buf, "finish") == 0 || strcmp(buf, "f") == 0)
    {
      dbg->mode = OAK_DEBUG_MODE_FINISH;
      dbg->step_line = current_line;
      dbg->step_source = current_source;
      dbg->step_frame_count = vm->frame_count;
      return;
    }
    if (strncmp(buf, "break ", 6) == 0 || strncmp(buf, "b ", 2) == 0)
    {
      const char* arg = buf[0] == 'b' && buf[1] == ' ' ? buf + 2 : buf + 6;
      int line = 0;
      if (!parse_positive_int(arg, &line))
      {
        fprintf(out, "usage: break <line>\n");
        continue;
      }
      const int id = oak_debugger_add_breakpoint(dbg, line, current_source);
      if (id < 0)
      {
        fprintf(out, "error: out of memory setting breakpoint\n");
        continue;
      }
      fprintf(out, "breakpoint #%d at %s:%d\n", id,
              current_source ? current_source : "*", line);
      continue;
    }
    if (strncmp(buf, "delete ", 7) == 0 || strncmp(buf, "d ", 2) == 0)
    {
      const char* arg = buf[0] == 'd' && buf[1] == ' ' ? buf + 2 : buf + 7;
      int id = 0;
      if (!parse_positive_int(arg, &id))
      {
        fprintf(out, "usage: delete <id>\n");
        continue;
      }
      if (oak_debugger_remove_breakpoint(dbg, id))
        fprintf(out, "deleted breakpoint #%d\n", id);
      else
        fprintf(out, "no breakpoint #%d\n", id);
      continue;
    }
    if (strcmp(buf, "breakpoints") == 0)
    {
      cmd_breakpoints(dbg);  /* uses dbg->out */
      continue;
    }
    if (strncmp(buf, "print ", 6) == 0 || strncmp(buf, "p ", 2) == 0)
    {
      const char* arg = buf[0] == 'p' && buf[1] == ' ' ? buf + 2 : buf + 6;
      while (*arg == ' ')
        ++arg;
      cmd_print(vm, arg, out);
      continue;
    }
    if (strcmp(buf, "locals") == 0)
    {
      cmd_locals(vm, out);
      continue;
    }
    if (strcmp(buf, "backtrace") == 0 || strcmp(buf, "bt") == 0)
    {
      cmd_backtrace(vm, out);
      continue;
    }
    if (strcmp(buf, "stack") == 0)
    {
      cmd_stack(vm, out);
      continue;
    }
    if (strcmp(buf, "list") == 0 || strcmp(buf, "l") == 0)
    {
      cmd_list(dbg, vm);
      continue;
    }
    if (strcmp(buf, "disassemble") == 0 || strcmp(buf, "dis") == 0)
    {
      cmd_disassemble(vm, out);
      continue;
    }
    if (strcmp(buf, "help") == 0 || strcmp(buf, "h") == 0)
    {
      cmd_help(out);
      continue;
    }
    if (strcmp(buf, "quit") == 0 || strcmp(buf, "q") == 0)
    {
      dbg->quit_requested = 1;
      return;
    }
    fprintf(out, "unknown command: '%s' (type 'help' for commands)\n", buf);
  }
}

/* ── debug hook ────────────────────────────────────────────────────── */

oak_debug_action_t oak_debugger_hook(oak_vm_t* vm, void* ctx)
{
  oak_debugger_t* dbg = (oak_debugger_t*)ctx;

  if (dbg->dap_mode)
    oak_dap_poll(dbg, vm);

  if (dbg->quit_requested)
    return OAK_DEBUG_HALT;

  const oak_chunk_t* chunk = vm->chunk;
  if (!chunk->debug || !chunk->debug->locations)
    return OAK_DEBUG_CONTINUE;

  if (!ip_in_chunk(vm->ip, chunk))
    return OAK_DEBUG_CONTINUE;

  const usize offset = (usize)(vm->ip - oak_chunk_code(chunk));
  const int line = oak_chunk_loc(chunk, offset).line;
  const char* src = chunk->debug->source_name;

  /* Re-arm breakpoint suppression on backward jumps (loop re-entry),
   * chunk switches, or frame count changes (function entry/exit). */
  if (chunk != dbg->last_stopped_chunk ||
      offset < dbg->prev_offset ||
      vm->frame_count != dbg->prev_frame_count)
  {
    dbg->last_stopped_offset = (usize)-1;
    dbg->last_stopped_chunk = OAK_NULL;
  }
  dbg->prev_offset = offset;
  dbg->prev_frame_count = vm->frame_count;

  /* Suppress: same line as last stop AND offset hasn't looped back. */
  const int suppressed =
      (dbg->last_stopped_chunk == chunk &&
       dbg->last_stopped_offset != (usize)-1 &&
       line == oak_chunk_loc(chunk, dbg->last_stopped_offset).line);

  int should_break = 0;
  const char* stop_reason = "breakpoint";

  if (dbg->pause_requested)
  {
    dbg->pause_requested = 0;
    should_break = 1;
    stop_reason = "pause";
  }
  else if (dbg->initial_break)
  {
    dbg->initial_break = 0;
    should_break = 1;
    stop_reason = "entry";
  }
  else
  {
    switch (dbg->mode)
    {
      case OAK_DEBUG_MODE_RUN:
        if (!suppressed)
          should_break = hit_breakpoint(dbg, line, src);
        break;
      case OAK_DEBUG_MODE_STEP:
      {
        stop_reason = "step";
        const int same_origin =
            (line == dbg->step_line && source_eq(src, dbg->step_source) &&
             vm->frame_count == dbg->step_frame_count);
        should_break = (line > 0 && !same_origin);
        break;
      }
      case OAK_DEBUG_MODE_NEXT:
      {
        stop_reason = "step";
        const int same_origin =
            (line == dbg->step_line && source_eq(src, dbg->step_source));
        should_break = (line > 0 && !same_origin &&
                        vm->frame_count <= dbg->step_frame_count);
        if (!should_break && line > 0 && !suppressed)
          should_break = hit_breakpoint(dbg, line, src);
        break;
      }
      case OAK_DEBUG_MODE_FINISH:
      {
        stop_reason = "step";
        should_break = (line > 0 &&
                        vm->frame_count < dbg->step_frame_count);
        if (!should_break && line > 0 && !suppressed)
          should_break = hit_breakpoint(dbg, line, src);
        break;
      }
    }
  }

  if (!should_break)
    return OAK_DEBUG_CONTINUE;

  dbg->last_stopped_offset = offset;
  dbg->last_stopped_chunk = chunk;
  if (dbg->dap_mode)
    oak_dap_stopped(dbg, vm, stop_reason);
  else
  {
    fprintf(dbg->out, "stopped at %s:%d\n", src ? src : "?", line);
    debugger_repl(dbg, vm, line, src);
  }

  return dbg->quit_requested ? OAK_DEBUG_HALT : OAK_DEBUG_CONTINUE;
}
