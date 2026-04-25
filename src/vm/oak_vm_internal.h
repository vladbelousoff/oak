#pragma once

#include "oak_bind.h"
#include "oak_log.h"
#include "oak_value.h"
#include "oak_vm.h"

static inline void oak_vm_push(struct oak_vm_t* vm,
                               const struct oak_value_t value)
{
  if (vm->sp >= vm->stack + OAK_STACK_MAX)
  {
    vm->had_stack_overflow = 1;
    return;
  }
  oak_value_incref(value);
  *vm->sp++ = value;
}

/* Push a value whose reference count already accounts for the new stack
 * ownership (i.e. take ownership without an extra incref). Use for values
 * just produced by oak_*_new / native fn return / similar fresh allocations
 * whose only outstanding reference is being transferred to the stack. */
static inline void oak_vm_push_owned(struct oak_vm_t* vm,
                                     const struct oak_value_t value)
{
  if (vm->sp >= vm->stack + OAK_STACK_MAX)
  {
    vm->had_stack_overflow = 1;
    return;
  }
  *vm->sp++ = value;
}

static inline struct oak_value_t oak_vm_pop(struct oak_vm_t* vm)
{
  oak_assert(vm->sp > vm->stack);
  return *--vm->sp;
}

static inline struct oak_value_t oak_vm_peek(const struct oak_vm_t* vm,
                                             const int distance)
{
  return vm->sp[-1 - distance];
}

static inline u16 oak_vm_read(struct oak_vm_t* vm, const int n)
{
  u16 val = 0;
  for (int i = 0; i < n; i++)
    val = (u16)((val << 8) | *vm->ip++);
  return val;
}

static inline u16 oak_vm_read_u16(struct oak_vm_t* vm)
{
  const u16 hi = *vm->ip++;
  const u16 lo = *vm->ip++;
  return (u16)((hi << 8) | lo);
}

static inline u32 oak_vm_read_u32(struct oak_vm_t* vm)
{
  const u32 b0 = *vm->ip++;
  const u32 b1 = *vm->ip++;
  const u32 b2 = *vm->ip++;
  const u32 b3 = *vm->ip++;
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

const char* oak_vm_value_kind_desc(struct oak_value_t v);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void oak_vm_runtime_error(const struct oak_vm_t* vm, const char* fmt, ...);

enum oak_vm_result_t oak_vm_numeric_binary(struct oak_vm_t* vm,
                                           u8 op,
                                           struct oak_value_t a,
                                           struct oak_value_t b);

enum oak_vm_result_t oak_vm_numeric_compare(struct oak_vm_t* vm,
                                            u8 op,
                                            struct oak_value_t a,
                                            struct oak_value_t b);

enum oak_vm_result_t oak_vm_op_call(struct oak_vm_t* vm);
enum oak_vm_result_t oak_vm_op_return(struct oak_vm_t* vm);
