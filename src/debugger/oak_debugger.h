#pragma once

#include "oak_allocator.h"
#include "oak_container.h"
#include "oak_export.h"
#include "oak_file_map.h"
#include "oak_vm.h"

#include <stdio.h>

typedef enum oak_debug_step_mode oak_debug_step_mode_t;
enum oak_debug_step_mode
{
  OAK_DEBUG_MODE_RUN,
  OAK_DEBUG_MODE_STEP,
  OAK_DEBUG_MODE_NEXT,
  OAK_DEBUG_MODE_FINISH,
};

typedef struct oak_breakpoint oak_breakpoint_t;
struct oak_breakpoint
{
  int id;
  int line;
  const char* source_name;
  int enabled;
};

typedef struct oak_debugger oak_debugger_t;
struct oak_debugger
{
  oak_allocator_t* allocator;
  oak_debug_step_mode_t mode;
  int step_frame_count;
  int step_line;
  const char* step_source;

  /* oak_breakpoint_t */
  oak_container_t* breakpoints;
  int next_bp_id;

  oak_file_map_t source_map;
  const char* cached_source_path;
  /* int: byte offset of the start of each line in the cached source. */
  oak_container_t* line_offsets;

  usize last_stopped_offset;
  const oak_chunk_t* last_stopped_chunk;
  usize prev_offset;
  int prev_frame_count;

  int initial_break;
  int quit_requested;
  int pause_requested;
  int dap_mode;
  void* dap_ctx;

  /* Command input and output streams. Default to stdin/stdout; tests redirect
   * them to in-memory files instead of reassigning the globals (which is not
   * portable â€” stdin/stdout are not l-values under MSVC). */
  FILE* in;
  FILE* out;
};

OAK_API void oak_debugger_init(oak_debugger_t* dbg,
                               oak_allocator_t* allocator);
OAK_API void oak_debugger_free(oak_debugger_t* dbg);

OAK_API oak_debug_action_t oak_debugger_hook(oak_vm_t* vm,
                                                  void* ctx);

OAK_API int oak_debugger_add_breakpoint(oak_debugger_t* dbg, int line,
                                       const char* source_name);
OAK_API int oak_debugger_remove_breakpoint(oak_debugger_t* dbg,
                                           int id);

OAK_API void oak_debugger_clear_breakpoints(oak_debugger_t* dbg,
                                            const char* source_name);
