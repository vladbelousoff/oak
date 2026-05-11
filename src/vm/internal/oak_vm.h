#pragma once

#include "oak_bind.h"
#include "oak_log.h"
#include "oak_module.h"
#include "oak_value.h"
#include <oak_vm.h>

void oak_vm_report_stack_overflow(const struct oak_vm_t* vm);

static inline enum oak_vm_result_t oak_vm_push(struct oak_vm_t* vm,
                                               const struct oak_value_t value)
{
  if (vm->sp >= vm->stack + OAK_STACK_MAX)
  {
    oak_vm_report_stack_overflow(vm);
    return OAK_VM_RUNTIME_ERROR;
  }
  oak_value_incref(value);
  *vm->sp++ = value;
  return OAK_VM_OK;
}

/* Push a value whose reference count already accounts for the new stack
 * ownership (i.e. take ownership without an extra incref). Use for values
 * just produced by oak_*_new / native fn return / similar fresh allocations
 * whose only outstanding reference is being transferred to the stack. On
 * overflow the caller is responsible for releasing `value` (see TRY_PUSH_OWNED
 * below; otherwise the fresh reference would leak). */
static inline enum oak_vm_result_t
oak_vm_push_owned(struct oak_vm_t* vm, const struct oak_value_t value)
{
  if (vm->sp >= vm->stack + OAK_STACK_MAX)
  {
    oak_vm_report_stack_overflow(vm);
    return OAK_VM_RUNTIME_ERROR;
  }
  *vm->sp++ = value;
  return OAK_VM_OK;
}

#define OAK_VM_TRY(expr)                                                       \
  do                                                                           \
  {                                                                            \
    const enum oak_vm_result_t _r = (expr);                                    \
    if (_r != OAK_VM_OK)                                                       \
      return _r;                                                               \
  } while (0)

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

static inline u8 oak_vm_read_u8(struct oak_vm_t* vm)
{
  return *vm->ip++;
}

static inline u16 oak_vm_read_u16(struct oak_vm_t* vm)
{
  const u16 hi = *vm->ip++;
  const u16 lo = *vm->ip++;
  return (u16)((hi << 8) | lo);
}

const char* oak_vm_value_kind_desc(struct oak_value_t v);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void oak_vm_runtime_error(const struct oak_vm_t* vm, const char* fmt, ...);

enum oak_vm_result_t oak_vm_numeric_binary(struct oak_vm_t* vm,
                                           u8 binop,
                                           struct oak_value_t a,
                                           struct oak_value_t b);

enum oak_vm_result_t oak_vm_numeric_compare(struct oak_vm_t* vm,
                                            u8 binop,
                                            struct oak_value_t a,
                                            struct oak_value_t b);

enum oak_vm_result_t oak_vm_op_call(struct oak_vm_t* vm);
enum oak_vm_result_t oak_vm_op_call_virtual(struct oak_vm_t* vm);
enum oak_vm_result_t oak_vm_op_return(struct oak_vm_t* vm);

/* oak_vm_stack.c — stack and local-variable opcodes */
enum oak_vm_result_t vm_stack_dispatch(struct oak_vm_t* vm,
                                       struct oak_chunk_t* chunk,
                                       u8 instruction);

/* oak_vm_control.c — jump and loop opcodes */
enum oak_vm_result_t vm_control_dispatch(struct oak_vm_t* vm, u8 instruction);

/* oak_vm_object.c — array / map / record / field opcodes */
enum oak_vm_result_t vm_object_dispatch(struct oak_vm_t* vm,
                                        struct oak_chunk_t* chunk,
                                        u8 instruction);
