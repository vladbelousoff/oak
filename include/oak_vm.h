#pragma once

#include "oak_chunk.h"
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
  uint8_t* return_ip;
  size_t caller_stack_base;
  size_t fn_slot;
};

struct oak_vm_t
{
  struct oak_chunk_t* chunk;
  uint8_t* ip;
  struct oak_value_t stack[OAK_STACK_MAX];
  struct oak_value_t* sp;
  size_t stack_base;
  struct oak_call_frame_t frames[OAK_FRAMES_MAX];
  int frame_count;
};

void oak_vm_init(struct oak_vm_t* vm);
void oak_vm_free(struct oak_vm_t* vm);

enum oak_vm_result_t oak_vm_run(struct oak_vm_t* vm, struct oak_chunk_t* chunk);
