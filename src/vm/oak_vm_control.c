#include "internal/oak_vm.h"

enum oak_vm_result_t vm_control_dispatch(struct oak_vm_t* vm, u8 instruction)
{
  switch (instruction)
  {
    case OAK_OP_JUMP:
    {
      const u16 offset = oak_vm_read_u16(vm);
      vm->ip += offset;
      break;
    }
    case OAK_OP_JUMP_IF_FALSE:
    {
      const u16 offset = oak_vm_read_u16(vm);
      struct oak_value_t cond = oak_vm_pop(vm);
      if (!oak_is_truthy(cond))
        vm->ip += offset;
      oak_value_decref(cond);
      break;
    }
    case OAK_OP_JUMP_IF_TRUE:
    {
      const u16 offset = oak_vm_read_u16(vm);
      struct oak_value_t cond = oak_vm_pop(vm);
      if (oak_is_truthy(cond))
        vm->ip += offset;
      oak_value_decref(cond);
      break;
    }
    case OAK_OP_LOOP:
    {
      const u16 offset = oak_vm_read_u16(vm);
      vm->ip -= offset;
      break;
    }
    default:
      oak_vm_runtime_error(
          vm, "internal error: unknown opcode 0x%02x", instruction);
      return OAK_VM_RUNTIME_ERROR;
  }
  return OAK_VM_OK;
}
