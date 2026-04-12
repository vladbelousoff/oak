#pragma once

#include "oak_chunk.h"
#include "oak_value.h"

#define OAK_STACK_MAX 256

enum oak_vm_result_t
{
  OAK_VM_OK,
  OAK_VM_COMPILE_ERROR,
  OAK_VM_RUNTIME_ERROR,
};

struct oak_vm_t
{
  struct oak_chunk_t* chunk;
  uint8_t* ip;
  struct oak_value_t stack[OAK_STACK_MAX];
  struct oak_value_t* sp;
};

void oak_vm_init(struct oak_vm_t* vm);
void oak_vm_free(struct oak_vm_t* vm);

enum oak_vm_result_t oak_vm_run(struct oak_vm_t* vm, struct oak_chunk_t* chunk);
