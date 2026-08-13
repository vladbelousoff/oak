#include "internal/oak_vm.h"

static oak_vm_result_t vm_call_native(oak_vm_t* vm,
                                           const u8 argc,
                                           const usize fn_slot,
                                           oak_value_t* arg_base,
                                           const oak_value_t fn_val)
{
  oak_obj_native_fn_t* native = oak_as_native_fn(fn_val);
  if ((int)argc != native->arity)
  {
    oak_vm_runtime_error(vm,
                         "function arity mismatch (expected %d, got %u)",
                         native->arity,
                         (unsigned)argc);
    return OAK_VM_RUNTIME_ERROR;
  }

  for (int hi = 0; hi < native->attr_hook_count; ++hi)
  {
    oak_native_ctx_t hook_ctx = { .vm = vm, .allocator = vm->allocator };
    const oak_fn_call_result_t r =
        native->attr_hooks[hi].cb(&hook_ctx,
                                  native->name,
                                  arg_base,
                                  (int)argc,
                                  native->attr_hooks[hi].ud);
    if (r != OAK_FN_CALL_OK)
    {
      oak_vm_runtime_error(vm,
                           "attribute hook aborted call to '%s'",
                           native->name ? native->name : "<anonymous>");
      return OAK_VM_RUNTIME_ERROR;
    }
  }

  oak_native_ctx_t nctx = { .vm = vm,
                                   .allocator = vm->allocator,
                                   .user_data = native->user_data };
  oak_value_t result = OAK_VALUE_NONE;
  const oak_fn_call_result_t err =
      native->fn(&nctx, arg_base, (int)argc, &result);
  if (err != OAK_FN_CALL_OK)
  {
    oak_vm_runtime_error(vm,
                         "native function '%s' failed",
                         native->name ? native->name : "<anonymous>");
    return OAK_VM_RUNTIME_ERROR;
  }

  if (!oak_vm_value_can_enter(vm, result))
  {
    oak_value_decref(result);
    return OAK_VM_RUNTIME_ERROR;
  }

  oak_value_decref(fn_val);
  for (u8 i = 0; i < argc; ++i)
    oak_value_decref(arg_base[i]);

  vm->stack[fn_slot] = result;
  vm->sp = vm->stack + fn_slot + 1u;

  return OAK_VM_OK;
}

static oak_vm_result_t vm_call_bytecode(oak_vm_t* vm,
                                             const u8 argc,
                                             const usize fn_slot,
                                             const oak_value_t fn_val)
{
  oak_obj_fn_t* fn = oak_as_fn(fn_val);
  if (fn->arity != (int)argc)
  {
    oak_vm_runtime_error(vm,
                         "function arity mismatch (expected %d, got %u)",
                         fn->arity,
                         (unsigned)argc);
    return OAK_VM_RUNTIME_ERROR;
  }

  for (int hi = 0; hi < fn->attr_hook_count; ++hi)
  {
    oak_value_t* arg_base = vm->stack + fn_slot + 1;
    oak_native_ctx_t hook_ctx = { .vm = vm, .allocator = vm->allocator };
    const oak_fn_call_result_t r = fn->attr_hooks[hi].cb(
        &hook_ctx, fn->name, arg_base, (int)argc, fn->attr_hooks[hi].ud);
    if (r != OAK_FN_CALL_OK)
    {
      oak_vm_runtime_error(vm, "attribute hook aborted function call");
      return OAK_VM_RUNTIME_ERROR;
    }
  }

  if (vm->frame_count >= OAK_FRAMES_MAX)
  {
    oak_vm_runtime_error(
        vm, "call stack overflow (max %d frames)", OAK_FRAMES_MAX);
    return OAK_VM_RUNTIME_ERROR;
  }

  /* Cross-module call?  If the fn's owning module differs from the currently
   * executing chunk, switch chunks and remember the original on the frame so
   * OP_RETURN can restore it. */
  oak_chunk_t* target_chunk = vm->chunk;
  if (vm->modules && fn->module_id != 0xFFFFu &&
      fn->module_id != vm->chunk->module_id)
  {
    oak_module_t* target_mod =
        oak_module_registry_get(vm->modules, fn->module_id);
    if (!target_mod || !target_mod->chunk)
    {
      oak_vm_runtime_error(vm,
                           "internal: cross-module call to unloaded module");
      return OAK_VM_RUNTIME_ERROR;
    }
    target_chunk = target_mod->chunk;
  }

  oak_call_frame_t* frame = &vm->frames[vm->frame_count++];
  frame->return_ip = vm->ip;
  frame->caller_stack_base = vm->stack_base;
  frame->fn_slot = fn_slot;
  frame->return_chunk = vm->chunk;
  vm->stack_base = fn_slot + 1u;
  vm->chunk = target_chunk;
  vm->ip = OAK_DATA(u8, target_chunk->code) + fn->code_offset;
  return OAK_VM_OK;
}

oak_vm_result_t oak_vm_op_call_virtual(oak_vm_t* vm)
{
  const u8 vtable_slot = oak_vm_read_u8(vm);
  const u8 arity = oak_vm_read_u8(vm); /* includes self */
  const usize depth = (usize)(vm->sp - vm->stack);
  /* arity counts the receiver, so 0 is malformed bytecode — and would
   * underflow n_args below. */
  if (arity == 0 || depth < (usize)arity)
  {
    oak_vm_runtime_error(vm, "stack underflow in virtual call");
    return OAK_VM_RUNTIME_ERROR;
  }

  /* The interface object is at the receiver position (sp - arity). */
  const usize recv_pos = depth - (usize)arity;
  const oak_value_t interface_obj_val = vm->stack[recv_pos];
  if (!oak_is_interface_object(interface_obj_val))
  {
    oak_vm_runtime_error(
        vm,
        "CALL_VIRTUAL: receiver is not an interface object, got %s",
        oak_vm_value_kind_desc(interface_obj_val));
    return OAK_VM_RUNTIME_ERROR;
  }
  const oak_obj_interface_object_t* to =
      oak_as_interface_object(interface_obj_val);
  if ((usize)vtable_slot >= to->vtable->length)
  {
    oak_vm_runtime_error(
        vm, "CALL_VIRTUAL: vtable slot %u out of range", (unsigned)vtable_slot);
    return OAK_VM_RUNTIME_ERROR;
  }

  const oak_value_t fn_val = to->vtable->items[vtable_slot];
  const oak_value_t concrete_val = to->value;

  if (!oak_is_fn(fn_val))
  {
    oak_vm_runtime_error(vm, "CALL_VIRTUAL: vtable entry is not a function");
    return OAK_VM_RUNTIME_ERROR;
  }
  if (!oak_vm_value_can_enter(vm, fn_val) ||
      !oak_vm_value_can_enter(vm, concrete_val))
    return OAK_VM_RUNTIME_ERROR;

  /* Make room for the fn value by shifting everything from recv_pos onwards

   * * right by one slot, then insert fn and unwrapped concrete value. */
  if (vm->sp >= vm->stack + OAK_STACK_MAX)
  {
    oak_vm_runtime_error(vm, "stack overflow in virtual call setup");
    return OAK_VM_RUNTIME_ERROR;
  }
  /* Shift the args (everything after recv_pos) right by 1. */
  const usize n_args = (usize)arity - 1u; /* args after receiver */
  for (usize i = n_args; i > 0; --i)
    vm->stack[recv_pos + 1u + i] = vm->stack[recv_pos + i];
  vm->sp++;

  /* Incref fn and concrete; decref the interface object (we replaced it). */
  oak_value_incref(fn_val);
  oak_value_incref(concrete_val);
  oak_value_decref(interface_obj_val);

  vm->stack[recv_pos] = fn_val;
  vm->stack[recv_pos + 1u] = concrete_val;

  /* Now dispatch exactly like OP_CALL with fn at recv_pos. */
  const usize fn_slot = recv_pos;
  oak_value_t* arg_base = &vm->stack[fn_slot + 1u];
  if (oak_is_native_fn(fn_val))
    return vm_call_native(vm, arity, fn_slot, arg_base, fn_val);
  return vm_call_bytecode(vm, arity, fn_slot, fn_val);
}

oak_vm_result_t oak_vm_op_call_with_argc(oak_vm_t* vm,
                                              const u8 argc)
{
  const usize depth = (usize)(vm->sp - vm->stack);
  if (depth < (usize)argc + 1u)
  {
    oak_vm_runtime_error(vm, "stack underflow in call");
    return OAK_VM_RUNTIME_ERROR;
  }

  const usize fn_slot = depth - (usize)argc - 1u;
  const oak_value_t fn_val = vm->stack[fn_slot];
  oak_value_t* arg_base = &vm->stack[fn_slot + 1u];

  if (oak_is_native_fn(fn_val))
    return vm_call_native(vm, argc, fn_slot, arg_base, fn_val);
  if (!oak_is_fn(fn_val))
  {
    oak_vm_runtime_error(vm, "call target is not a function");
    return OAK_VM_RUNTIME_ERROR;
  }
  return vm_call_bytecode(vm, argc, fn_slot, fn_val);
}

oak_vm_result_t oak_vm_op_call(oak_vm_t* vm)
{
  return oak_vm_op_call_with_argc(vm, oak_vm_read_u8(vm));
}

static u8 halt_trampoline[] = { 0 /* OAK_OP_HALT */ };

static oak_vm_result_t oak_vm_call_impl(oak_vm_t* vm,
                                             oak_value_t fn_val,
                                             const oak_value_t* args,
                                             int argc,
                                             oak_value_t* out_result)
{
  if (!vm->chunk)
  {
    oak_vm_runtime_error(vm, "oak_vm_call: no active chunk");
    return OAK_VM_RUNTIME_ERROR;
  }

  u8* saved_ip = vm->ip;
  oak_chunk_t* saved_chunk = vm->chunk;
  usize saved_stack_base = vm->stack_base;
  oak_value_t* const entry_sp = vm->sp;

  /* Point IP at the halt trampoline so that when the called function returns,
   * the VM sees HALT and oak_vm_resume returns OAK_VM_OK. */
  vm->ip = halt_trampoline;

  oak_vm_result_t r = oak_vm_push(vm, fn_val);
  for (int i = 0; r == OAK_VM_OK && i < argc; ++i)
    r = oak_vm_push(vm, args[i]);
  if (r == OAK_VM_OK)
    r = oak_vm_op_call_with_argc(vm, (u8)argc);
  if (r != OAK_VM_OK)
  {
    /* Call setup failed before any frame was pushed: drop whatever made it
     * onto the stack and put the VM back exactly as we found it, so the
     * caller can keep using it. */
    while (vm->sp > entry_sp)
      oak_value_decref(*--vm->sp);
    vm->ip = saved_ip;
    vm->chunk = saved_chunk;
    vm->stack_base = saved_stack_base;
    return r;
  }

  r = oak_vm_resume(vm);

  if (r == OAK_VM_OK && out_result)
  {
    if (vm->sp > vm->stack)
      *out_result = oak_vm_pop(vm);
    else
      *out_result = OAK_VALUE_NONE;
  }

  vm->ip = saved_ip;
  vm->chunk = saved_chunk;
  vm->stack_base = saved_stack_base;
  return r;
}

oak_vm_result_t oak_vm_call(oak_vm_t* vm,
                            oak_value_t fn_val,
                            const oak_value_t* args,
                            int argc,
                            oak_value_t* out_result)
{
  oak_vm_clear_last_error(vm);
  return oak_vm_call_impl(vm, fn_val, args, argc, out_result);
}

oak_vm_result_t oak_vm_op_return(oak_vm_t* vm)
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

  oak_value_t result = oak_vm_pop(vm);
  oak_call_frame_t* frame = &vm->frames[--vm->frame_count];
  const usize fn_slot = frame->fn_slot;

  for (usize i = fn_slot; i < depth_before - 1u; ++i)
    oak_value_decref(vm->stack[i]);

  vm->stack[fn_slot] = result;
  vm->sp = vm->stack + fn_slot + 1u;
  vm->ip = frame->return_ip;
  vm->stack_base = frame->caller_stack_base;
  if (frame->return_chunk)
    vm->chunk = frame->return_chunk;
  return OAK_VM_OK;
}
