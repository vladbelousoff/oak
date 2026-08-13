#pragma once

#include "oak_allocator.h"
#include "oak_chunk.h"
#include "oak_diagnostic.h"
#include "oak_export.h"
#include "oak_value.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAK_STACK_MAX  256
#define OAK_FRAMES_MAX 64

/* Outcome of a VM run / call.  COMPILE_ERROR is reported when the chunk
 * passed to oak_vm_run was produced from a failed compile. */
typedef enum oak_vm_result oak_vm_result_t;
enum oak_vm_result
{
  OAK_VM_OK,
  OAK_VM_COMPILE_ERROR,
  OAK_VM_RUNTIME_ERROR,
  OAK_VM_DEBUG_HALT,
};

/* Action returned by a debug hook to the VM execution loop. */
typedef enum oak_debug_action oak_debug_action_t;
enum oak_debug_action
{
  OAK_DEBUG_CONTINUE,
  OAK_DEBUG_HALT,
};

typedef struct oak_call_frame oak_call_frame_t;
struct oak_call_frame
{
  u8* return_ip;
  usize caller_stack_base;
  usize fn_slot;
  /* Chunk to restore on return.  When the call did not switch chunks
   * (intra-module), this is the same as the caller's chunk. */
  oak_chunk_t* return_chunk;
};

typedef struct oak_module_registry oak_module_registry_t;

/* Debug hook descriptor.  Allocated and owned by the caller; the VM borrows
 * a pointer and never frees it.  Defined here so the struct can be extended
 * in the future without changing the size of oak_vm_t. */
typedef struct oak_vm_debug_hook oak_vm_debug_hook_t;
struct oak_vm_debug_hook
{
  oak_debug_action_t (*fn)(oak_vm_t* vm, void* ctx);
  void* ctx;
};

typedef struct oak_vm oak_vm_t;
struct oak_vm
{
  oak_chunk_t* chunk;
  u8* ip;
  oak_value_t stack[OAK_STACK_MAX];
  oak_value_t* sp;
  usize stack_base;
  oak_call_frame_t frames[OAK_FRAMES_MAX];
  int frame_count;
  /* Optional: when set, the VM can resolve cross-module references via
   * OP_GET_MODULE_FN and switch chunks on cross-module CALL/RETURN. */
  oak_module_registry_t* modules;
  oak_allocator_t* allocator;
  /* Optional embedder context. Borrowed, never freed by the VM. Native
   * callbacks can recover their owning embedder via ctx->vm->user_data,
   * which lets multiple independent VMs coexist without sharing embedder
   * state. */
  void* user_data;
  /* Opaque debug hook; null when no debugger is attached.  Installed via
   * oak_vm_set_debug_hook.  Borrowed, never freed by the VM. */
  oak_vm_debug_hook_t* debug_hook;
  /* This VM's object table.  oak_vm_* allocation functions create objects in
   * oak_obj_tables[object_table].  Acquired by oak_vm_init, detached by
   * oak_vm_free (and recycled once its last object dies) — see the object
   * table registry in oak_value.h. */
  u32 object_table;
  /* Most recent runtime error, or line == 0 and an empty message when none.
   * Read it with oak_vm_last_error rather than touching this directly. */
  oak_diagnostic_t last_error;
  /* Bumped every time last_error is written.  The VM samples it around a
   * native call to tell "the callback reported its own error" from "the
   * callback just returned OAK_FN_CALL_RUNTIME_ERROR", so a native that
   * called oak_native_error keeps its message instead of having it replaced
   * by the generic one.  Wrapping is harmless: only equality matters. */
  u32 error_seq;
};

/* Zero-initialize a VM and wire it to an allocator.  The allocator pointer
 * is borrowed for the lifetime of the VM; it is not freed by oak_vm_free. */
OAK_API void oak_vm_init(oak_vm_t* vm,
                         oak_allocator_t* allocator);

/* Allocate values owned by `vm`, independently of which thread makes the
 * call.  A VM may be used from different threads between calls, but the VM
 * and its values must not be accessed concurrently.  The plain oak_*_new
 * constructors create process-shared table-0 values instead. */
OAK_API oak_obj_string_t* oak_vm_string_new(oak_vm_t* vm,
                                            const char* chars);
OAK_API oak_obj_string_t*
oak_vm_string_new_len(oak_vm_t* vm, const char* chars, usize length);
OAK_API oak_obj_string_t*
oak_vm_string_concat(oak_vm_t* vm,
                     const oak_obj_string_t* left,
                     const oak_obj_string_t* right);
OAK_API oak_obj_array_t* oak_vm_array_new(oak_vm_t* vm);
OAK_API oak_obj_map_t* oak_vm_map_new(oak_vm_t* vm);
OAK_API oak_obj_record_t*
oak_vm_record_new(oak_vm_t* vm,
                  int field_count,
                  const char* type_name,
                  const char* const* field_names);
OAK_API oak_value_t oak_vm_native_record_new(
    oak_vm_t* vm, const oak_bind_type_t* type, void* instance);
OAK_API oak_obj_string_t*
oak_vm_value_to_string(oak_vm_t* vm, oak_value_t value);
OAK_API oak_obj_string_t*
oak_vm_string_from_value_repr(oak_vm_t* vm, oak_value_t value);

/* Release any heap state owned by the VM.  Does not free chunks (those are
 * owned by their oak_compile_result_t / oak_module_t). */
OAK_API void oak_vm_free(oak_vm_t* vm);

/* Attach a module registry so cross-module CALL / GET_MODULE_FN opcodes can
 * resolve.  May be called before oak_vm_run; the pointer is borrowed. */
OAK_API void oak_vm_set_module_registry(oak_vm_t* vm,
                                        oak_module_registry_t* modules);

/* Attach a debug hook.  The hook is called before every instruction dispatch
 * when non-null.  The pointer is borrowed; the caller retains ownership. */
OAK_API void oak_vm_set_debug_hook(oak_vm_t* vm,
                                   oak_vm_debug_hook_t* hook);

/* Execute `chunk` from its entry point.  `chunk` is borrowed; the caller
 * retains ownership.  Returns OAK_VM_OK on a clean halt; on
 * OAK_VM_RUNTIME_ERROR the message is available from oak_vm_last_error. */
OAK_API oak_vm_result_t oak_vm_run(oak_vm_t* vm, oak_chunk_t* chunk);

/* Resume execution from the VM's current IP.  Use after a previous run
 * suspended (e.g. via a yielding native fn). */
OAK_API oak_vm_result_t oak_vm_resume(oak_vm_t* vm);

/* Attach `chunk` to the VM without executing anything, so that oak_vm_call can
 * be used on a VM that has not run a program.  `chunk` is borrowed.  Calling
 * oak_vm_run afterwards is fine and overrides this. */
OAK_API void oak_vm_prepare(oak_vm_t* vm, oak_chunk_t* chunk);

/* Call an Oak function from C.  Pushes fn_val and args onto the stack, sets up
 * a call frame, and runs the VM until the function returns.
 * out_result receives the return value (may be NULL to discard it).
 * The VM must have a chunk attached — either from a previous oak_vm_run or
 * from oak_vm_prepare.  On failure, see oak_vm_last_error. */
OAK_API oak_vm_result_t oak_vm_call(oak_vm_t* vm,
                                    oak_value_t fn_val,
                                    const oak_value_t* args,
                                    usize argc,
                                    oak_value_t* out_result);

/* The most recent runtime error, or NULL if none has occurred since the last
 * oak_vm_run / oak_vm_call.  Runtime errors are also written to stderr; this
 * is how an embedder gets the message and source location as data.  The
 * pointer is owned by the VM and valid until the next run or call. */
OAK_API const oak_diagnostic_t* oak_vm_last_error(const oak_vm_t* vm);

#ifdef __cplusplus
}
#endif
