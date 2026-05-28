#pragma once

#include "oak_allocator.h"
#include "oak_chunk.h"
#include "oak_export.h"
#include "oak_value.h"

#define OAK_STACK_MAX  256
#define OAK_FRAMES_MAX 64

/* Outcome of a VM run / call.  COMPILE_ERROR is reported when the chunk
 * passed to oak_vm_run was produced from a failed compile. */
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
  /* Chunk to restore on return.  When the call did not switch chunks
   * (intra-module), this is the same as the caller's chunk. */
  struct oak_chunk_t* return_chunk;
};

struct oak_module_registry_t;

struct oak_vm_t
{
  struct oak_chunk_t* chunk;
  u8* ip;
  struct oak_value_t stack[OAK_STACK_MAX];
  struct oak_value_t* sp;
  usize stack_base;
  struct oak_call_frame_t frames[OAK_FRAMES_MAX];
  int frame_count;
  /* Optional: when set, the VM can resolve cross-module references via
   * OP_GET_MODULE_FN and switch chunks on cross-module CALL/RETURN. */
  struct oak_module_registry_t* modules;
  struct oak_allocator_t* allocator;
};

/* Zero-initialize a VM and wire it to an allocator.  The allocator pointer
 * is borrowed for the lifetime of the VM; it is not freed by oak_vm_free. */
OAK_API void oak_vm_init(struct oak_vm_t* vm, struct oak_allocator_t* allocator);

/* Release any heap state owned by the VM.  Does not free chunks (those are
 * owned by their oak_compile_result_t / oak_module_t). */
OAK_API void oak_vm_free(struct oak_vm_t* vm);

/* Attach a module registry so cross-module CALL / GET_MODULE_FN opcodes can
 * resolve.  May be called before oak_vm_run; the pointer is borrowed. */
OAK_API void oak_vm_set_module_registry(struct oak_vm_t* vm,
                                        struct oak_module_registry_t* modules);

/* Execute `chunk` from its entry point.  `chunk` is borrowed; the caller
 * retains ownership.  Returns OAK_VM_OK on a clean halt. */
OAK_API enum oak_vm_result_t oak_vm_run(struct oak_vm_t* vm,
                                        struct oak_chunk_t* chunk);

/* Resume execution from the VM's current IP.  Use after a previous run
 * suspended (e.g. via a yielding native fn). */
OAK_API enum oak_vm_result_t oak_vm_resume(struct oak_vm_t* vm);

/* Call an Oak function from C.  Pushes fn_val and args onto the stack, sets up
 * a call frame, and runs the VM until the function returns.
 * out_result receives the return value (may be NULL to discard it).
 * The VM must have been initialized and a chunk must have been run at least
 * once (so that the chunk is set and the IP is valid). */
OAK_API enum oak_vm_result_t oak_vm_call(struct oak_vm_t* vm,
                                         struct oak_value_t fn_val,
                                         const struct oak_value_t* args,
                                         int argc,
                                         struct oak_value_t* out_result);
