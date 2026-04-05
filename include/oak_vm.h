#pragma once

#include "oak_chunk.h"
#include "oak_value.h"

#define OAK_STACK_MAX 256

typedef enum
{
  OAK_VM_OK,
  OAK_VM_COMPILE_ERROR,
  OAK_VM_RUNTIME_ERROR,
} oak_vm_result_t;

typedef struct
{
  oak_chunk_t* chunk;
  uint8_t* ip;
  oak_value_t stack[OAK_STACK_MAX];
  oak_value_t* sp;
} oak_vm_t;

void oak_vm_init(oak_vm_t* vm);
void oak_vm_free(oak_vm_t* vm);

oak_vm_result_t oak_vm_run(oak_vm_t* vm, oak_chunk_t* chunk);
