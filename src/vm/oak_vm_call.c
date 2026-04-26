#include "oak_vm_internal.h"

static enum oak_vm_result_t vm_call_native(struct oak_vm_t* vm,
                                           const u8 argc,
                                           const usize fn_slot,
                                           struct oak_value_t* arg_base,
                                           const struct oak_value_t fn_val)
{
  struct oak_obj_native_fn_t* native = oak_as_native_fn(fn_val);
  if ((int)argc != native->arity)
  {
    oak_vm_runtime_error(vm,
                         "function arity mismatch (expected %d, got %u)",
                         native->arity,
                         (unsigned)argc);
    return OAK_VM_RUNTIME_ERROR;
  }

  struct oak_native_ctx_t nctx = { .vm = vm };
  struct oak_value_t result;
  const enum oak_fn_call_result_t err =
      native->fn(&nctx, arg_base, (int)argc, &result);
  if (err != OAK_FN_CALL_OK)
  {
    oak_vm_runtime_error(vm,
                         "native function '%s' failed",
                         native->name ? native->name : "<anonymous>");
    return OAK_VM_RUNTIME_ERROR;
  }

  oak_value_decref(fn_val);
  for (u8 i = 0; i < argc; ++i)
    oak_value_decref(arg_base[i]);

  vm->stack[fn_slot] = result;
  vm->sp = vm->stack + fn_slot + 1u;

  return OAK_VM_OK;
}

static enum oak_vm_result_t vm_call_bytecode(struct oak_vm_t* vm,
                                             const u8 argc,
                                             const usize fn_slot,
                                             const struct oak_value_t fn_val)
{
  struct oak_obj_fn_t* fn = oak_as_fn(fn_val);
  if (fn->arity != (int)argc)
  {
    oak_vm_runtime_error(vm,
                         "function arity mismatch (expected %d, got %u)",
                         fn->arity,
                         (unsigned)argc);
    return OAK_VM_RUNTIME_ERROR;
  }

  if (vm->frame_count >= OAK_FRAMES_MAX)
  {
    oak_vm_runtime_error(
        vm, "call stack overflow (max %d frames)", OAK_FRAMES_MAX);
    return OAK_VM_RUNTIME_ERROR;
  }

  struct oak_call_frame_t* frame = &vm->frames[vm->frame_count++];
  frame->return_ip = vm->ip;
  frame->caller_stack_base = vm->stack_base;
  frame->fn_slot = fn_slot;
  vm->stack_base = fn_slot + 1u;
  vm->ip = vm->chunk->bytecode + fn->code_offset;
  return OAK_VM_OK;
}

enum oak_vm_result_t oak_vm_op_call(struct oak_vm_t* vm)
{
  const u8 argc = (u8)oak_vm_read(vm, 1);
  const usize depth = (usize)(vm->sp - vm->stack);
  if (depth < (usize)argc + 1u)
  {
    oak_vm_runtime_error(vm, "stack underflow in call");
    return OAK_VM_RUNTIME_ERROR;
  }

  const usize fn_slot = depth - (usize)argc - 1u;
  const struct oak_value_t fn_val = vm->stack[fn_slot];
  struct oak_value_t* arg_base = &vm->stack[fn_slot + 1u];

  if (oak_is_native_fn(fn_val))
    return vm_call_native(vm, argc, fn_slot, arg_base, fn_val);
  if (!oak_is_fn(fn_val))
  {
    oak_vm_runtime_error(vm, "call target is not a function");
    return OAK_VM_RUNTIME_ERROR;
  }
  return vm_call_bytecode(vm, argc, fn_slot, fn_val);
}

enum oak_vm_result_t oak_vm_op_return(struct oak_vm_t* vm)
{
  if (vm->frame_count == 0)
  {
    oak_vm_runtime_error(vm, "'return' outside of a function");
    return OAK_VM_RUNTIME_ERROR;
  }

  const usize depth_before = (usize)(vm->sp - vm->stack);
  if (depth_before == 0)
  {
    oak_vm_runtime_error(vm, "stack underflow in return");
    return OAK_VM_RUNTIME_ERROR;
  }

  struct oak_value_t result = oak_vm_pop(vm);
  struct oak_call_frame_t* frame = &vm->frames[--vm->frame_count];
  const usize fn_slot = frame->fn_slot;

  for (usize i = fn_slot; i < depth_before - 1u; ++i)
    oak_value_decref(vm->stack[i]);

  vm->stack[fn_slot] = result;
  vm->sp = vm->stack + fn_slot + 1u;
  vm->ip = frame->return_ip;
  vm->stack_base = frame->caller_stack_base;
  return OAK_VM_OK;
}
