#pragma once

#include "oak_allocator.h"
#include "oak_export.h"
#include "oak_file_map.h"
#include "oak_vm.h"

#include <stdio.h>

enum oak_debug_step_mode_t
{
  OAK_DEBUG_MODE_RUN,
  OAK_DEBUG_MODE_STEP,
  OAK_DEBUG_MODE_NEXT,
  OAK_DEBUG_MODE_FINISH,
};

struct oak_breakpoint_t
{
  int id;
  int line;
  const char* source_name;
  int enabled;
};

struct oak_debugger_t
{
  struct oak_allocator_t* allocator;
  enum oak_debug_step_mode_t mode;
  int step_frame_count;
  int step_line;
  const char* step_source;

  struct oak_breakpoint_t* breakpoints;
  int bp_count;
  int bp_capacity;
  int next_bp_id;

  struct oak_file_map_t source_map;
  const char* cached_source_path;
  int* line_offsets;
  int line_count;

  usize last_stopped_offset;
  const struct oak_chunk_t* last_stopped_chunk;
  usize prev_offset;
  int prev_frame_count;

  int initial_break;
  int quit_requested;

  /* Command input and output streams. Default to stdin/stdout; tests redirect
   * them to in-memory files instead of reassigning the globals (which is not
   * portable — stdin/stdout are not l-values under MSVC). */
  FILE* in;
  FILE* out;
};

OAK_API void oak_debugger_init(struct oak_debugger_t* dbg,
                               struct oak_allocator_t* allocator);
OAK_API void oak_debugger_free(struct oak_debugger_t* dbg);

OAK_API enum oak_debug_action_t oak_debugger_hook(struct oak_vm_t* vm,
                                                  void* ctx);

OAK_API int oak_debugger_add_breakpoint(struct oak_debugger_t* dbg, int line,
                                       const char* source_name);
OAK_API int oak_debugger_remove_breakpoint(struct oak_debugger_t* dbg,
                                           int id);
