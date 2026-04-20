#pragma once

#include "oak_log.h"
#include "oak_value.h"
#include "oak_vm.h"

static inline void oak_vm_push(struct oak_vm_t* vm,
                               const struct oak_value_t value)
{
  oak_assert(vm->sp < vm->stack + OAK_STACK_MAX);
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
  oak_assert(vm->sp < vm->stack + OAK_STACK_MAX);
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
    val = (val << 8) | *vm->ip++;
  return val;
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
