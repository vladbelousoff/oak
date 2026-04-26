#pragma once

#include "oak_chunk.h"
#include "oak_mem.h"
#include "oak_value.h"

#define OAK_STACK_MAX  256
#define OAK_FRAMES_MAX 64

enum oak_vm_result_t
{
  OAK_VM_OK,
  OAK_VM_COMPILE_ERROR,
  OAK_VM_RUNTIME_ERROR,
};

struct oak_call_frame_t
{
  u8* return_ip;
  usize caller_stack_base;
  usize fn_slot;
};

struct oak_vm_t
{
  struct oak_chunk_t* chunk;
  u8* ip;
  struct oak_value_t stack[OAK_STACK_MAX];
  struct oak_value_t* sp;
  usize stack_base;
  struct oak_call_frame_t frames[OAK_FRAMES_MAX];
  int frame_count;
  int had_stack_overflow;
};

void oak_vm_init(struct oak_vm_t* vm);
void oak_vm_free(struct oak_vm_t* vm);

enum oak_vm_result_t oak_vm_run(struct oak_vm_t* vm, struct oak_chunk_t* chunk);

/* For native callbacks: `oak_alloc` / `oak_free` site as the current Oak call
 * (chunk source_name and CALL line, or a fallback label if unset). */
struct oak_src_loc_t oak_vm_oak_mem_src_loc(const struct oak_vm_t* vm);
