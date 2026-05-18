#include "internal/oak_vm.h"

enum oak_vm_result_t vm_stack_dispatch(struct oak_vm_t* vm,
                                       struct oak_chunk_t* chunk,
                                       u8 instruction)
{
  switch (instruction)
  {
    case OAK_OP_CONSTANT:
    {
      const u16 idx = oak_vm_read_u16(vm);
      oak_assert((usize)idx < chunk->const_count);
      OAK_VM_TRY(oak_vm_push(vm, chunk->constants[idx]));
      break;
    }
    case OAK_OP_PUSH_INT8:
    {
      const signed char val = (signed char)oak_vm_read_u8(vm);
      OAK_VM_TRY(oak_vm_push_owned(vm, OAK_VALUE_I32((int)val)));
      break;
    }
    case OAK_OP_TRUE:
      OAK_VM_TRY(oak_vm_push(vm, OAK_VALUE_BOOL(1)));
      break;
    case OAK_OP_FALSE:
      OAK_VM_TRY(oak_vm_push(vm, OAK_VALUE_BOOL(0)));
      break;
    case OAK_OP_NONE:
      OAK_VM_TRY(oak_vm_push_owned(vm, OAK_VALUE_NONE));
      break;
    case OAK_OP_POP:
    {
      const struct oak_value_t val = oak_vm_pop(vm);
      oak_value_decref(val);
      break;
    }
    case OAK_OP_POP_N:
    {
      const u8 n = oak_vm_read_u8(vm);
      oak_assert((usize)(vm->sp - vm->stack) >= (usize)n);
      for (u8 i = 0; i < n; ++i)
        oak_value_decref(*--vm->sp);
      break;
    }
    case OAK_OP_GET_LOCAL:
    {
      const u8 slot = oak_vm_read_u8(vm);
      const usize idx = vm->stack_base + (usize)slot;
      if (idx >= OAK_STACK_MAX)
      {
        oak_vm_runtime_error(vm, "local slot out of range (slot %u)", slot);
        return OAK_VM_RUNTIME_ERROR;
      }
      OAK_VM_TRY(oak_vm_push(vm, vm->stack[idx]));
      break;
    }
    case OAK_OP_SET_LOCAL:
    {
      const u8 slot = oak_vm_read_u8(vm);
      const usize idx = vm->stack_base + (usize)slot;
      if (idx >= OAK_STACK_MAX)
      {
        oak_vm_runtime_error(vm, "local slot out of range (slot %u)", slot);
        return OAK_VM_RUNTIME_ERROR;
      }
      const struct oak_value_t new_val = oak_vm_pop(vm);
      const struct oak_value_t old_val = vm->stack[idx];
      vm->stack[idx] = new_val;
      oak_value_decref(old_val);
      break;
    }
    case OAK_OP_WEAKEN:
    {
      oak_assert(vm->sp > vm->stack);
      const struct oak_value_t value = vm->sp[-1];
      if (!oak_is_obj(value))
      {
        oak_vm_runtime_error(
            vm, "weak reference requires an object, got %s",
            oak_vm_value_kind_desc(value));
        return OAK_VM_RUNTIME_ERROR;
      }
      vm->sp[-1] = oak_value_weaken(value);
      oak_refcount_inc(&value.as.obj->weak_refcount);
      oak_value_decref(value);
      break;
    }
    case OAK_OP_INC_LOCAL:
    case OAK_OP_DEC_LOCAL:
    {
      const u8 slot = oak_vm_read_u8(vm);
      const usize idx = vm->stack_base + (usize)slot;
      if (idx >= OAK_STACK_MAX)
      {
        oak_vm_runtime_error(vm, "local slot out of range (slot %u)", slot);
        return OAK_VM_RUNTIME_ERROR;
      }
      const struct oak_value_t val = vm->stack[idx];
      if (!oak_is_number(val))
      {
        oak_vm_runtime_error(
            vm,
            "local increment/decrement expects a number, got %s",
            oak_vm_value_kind_desc(val));
        return OAK_VM_RUNTIME_ERROR;
      }
      if (oak_is_i32(val))
      {
        const int delta = instruction == OAK_OP_INC_LOCAL ? 1 : -1;
        vm->stack[idx] = OAK_VALUE_I32(oak_as_i32(val) + delta);
        break;
      }
      const float fdelta = instruction == OAK_OP_INC_LOCAL ? 1.0f : -1.0f;
      vm->stack[idx] = OAK_VALUE_F32(oak_as_f32(val) + fdelta);
      break;
    }
    default:
      oak_vm_runtime_error(
          vm, "internal error: unknown opcode 0x%02x", instruction);
      return OAK_VM_RUNTIME_ERROR;
  }
  return OAK_VM_OK;
}
