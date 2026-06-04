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
  OAK_VM_DEBUG_HALT,
};

/* Action returned by a debug hook to the VM execution loop. */
enum oak_debug_action_t
{
  OAK_DEBUG_CONTINUE,
  OAK_DEBUG_HALT,
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

/* Debug hook descriptor.  Allocated and owned by the caller; the VM borrows
 * a pointer and never frees it.  Defined here so the struct can be extended
 * in the future without changing the size of oak_vm_t. */
struct oak_vm_debug_hook_t
{
  enum oak_debug_action_t (*fn)(struct oak_vm_t* vm, void* ctx);
  void* ctx;
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
  /* Optional: when set, the VM can resolve cross-module references via
   * OP_GET_MODULE_FN and switch chunks on cross-module CALL/RETURN. */
  struct oak_module_registry_t* modules;
  struct oak_allocator_t* allocator;
  /* Optional embedder context. Borrowed, never freed by the VM. Native
   * callbacks can recover their owning embedder via ctx->vm->user_data,
   * which lets multiple independent VMs coexist without process globals. */
  void* user_data;
  /* Opaque debug hook; null when no debugger is attached.  Installed via
   * oak_vm_set_debug_hook.  Borrowed, never freed by the VM. */
  struct oak_vm_debug_hook_t* debug_hook;
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

/* Attach a debug hook.  The hook is called before every instruction dispatch
 * when non-null.  The pointer is borrowed; the caller retains ownership. */
OAK_API void oak_vm_set_debug_hook(struct oak_vm_t* vm,
                                   struct oak_vm_debug_hook_t* hook);

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
