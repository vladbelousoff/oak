#pragma once

#include "oak_bind.h"
#include "oak_log.h"
#include "oak_module.h"
#include "oak_value.h"
#include <oak_vm.h>

void oak_vm_report_stack_overflow(const oak_vm_t* vm);
const char* oak_vm_value_kind_desc(oak_value_t v);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void oak_vm_runtime_error(const oak_vm_t* vm, const char* fmt, ...);

static inline int oak_vm_value_can_enter(const oak_vm_t* vm,
                                         const oak_value_t value)
{
  if (oak_value_can_refcopy_to_table(value, vm->object_table))
    return 1;
  oak_vm_runtime_error(vm,
                       "cannot transfer object from VM table %u to VM table %u",
                       (unsigned)oak_value_obj_table(value),
                       (unsigned)vm->object_table);
  return 0;
}

static inline oak_vm_result_t oak_vm_push(oak_vm_t* vm,
                                               const oak_value_t value)
{
  if (vm->sp >= vm->stack + OAK_STACK_MAX)
  {
    oak_vm_report_stack_overflow(vm);
    return OAK_VM_RUNTIME_ERROR;
  }
  if (!oak_vm_value_can_enter(vm, value))
    return OAK_VM_RUNTIME_ERROR;
  oak_value_incref(value);
  *vm->sp++ = value;
  return OAK_VM_OK;
}

/* Push a value whose reference count already accounts for the new stack
 * ownership (i.e. take ownership without an extra incref). Use for values
 * just produced by oak_*_new / native fn return / similar fresh allocations
 * whose only outstanding reference is being transferred to the stack.  This
 * function releases `value` if the transfer fails. */
static inline oak_vm_result_t
oak_vm_push_owned(oak_vm_t* vm, const oak_value_t value)
{
  if (vm->sp >= vm->stack + OAK_STACK_MAX)
  {
    oak_value_decref(value);
    oak_vm_report_stack_overflow(vm);
    return OAK_VM_RUNTIME_ERROR;
  }
  if (!oak_vm_value_can_enter(vm, value))
  {
    oak_value_decref(value);
    return OAK_VM_RUNTIME_ERROR;
  }
  *vm->sp++ = value;
  return OAK_VM_OK;
}

static inline oak_value_t oak_vm_pop(oak_vm_t* vm)
{
  oak_assert(vm->sp > vm->stack);
  return *--vm->sp;
}

static inline oak_value_t oak_vm_peek(const oak_vm_t* vm,
                                             const int distance)
{
  return vm->sp[-1 - distance];
}

static inline u8 oak_vm_read_u8(oak_vm_t* vm)
{
  return *vm->ip++;
}

static inline u16 oak_vm_read_u16(oak_vm_t* vm)
{
  const u16 hi = *vm->ip++;
  const u16 lo = *vm->ip++;
  return (u16)((hi << 8) | lo);
}

/* i32 arithmetic wraps on overflow (two's complement) instead of invoking
 * signed-overflow UB. */
static inline int oak_i32_wrap_add(const int a, const int b)
{
  return (int)((u32)a + (u32)b);
}

static inline int oak_i32_wrap_sub(const int a, const int b)
{
  return (int)((u32)a - (u32)b);
}

static inline int oak_i32_wrap_mul(const int a, const int b)
{
  return (int)((u32)a * (u32)b);
}

static inline int oak_i32_wrap_neg(const int a)
{
  return (int)(0u - (u32)a);
}

oak_vm_result_t oak_vm_numeric_binary(oak_vm_t* vm,
                                           u8 binop,
                                           oak_value_t a,
                                           oak_value_t b);

oak_vm_result_t oak_vm_numeric_compare(oak_vm_t* vm,
                                            u8 binop,
                                            oak_value_t a,
                                            oak_value_t b);

oak_vm_result_t oak_vm_op_call(oak_vm_t* vm);
oak_vm_result_t oak_vm_op_call_with_argc(oak_vm_t* vm, u8 argc);
oak_vm_result_t oak_vm_op_call_virtual(oak_vm_t* vm);
oak_vm_result_t oak_vm_op_return(oak_vm_t* vm);

/* oak_vm_object.c — array / map / record / field opcodes */
oak_vm_result_t vm_object_dispatch(oak_vm_t* vm,
                                        oak_chunk_t* chunk,
                                        u8 instruction);
