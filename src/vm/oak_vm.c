#include "internal/oak_vm.h"

void oak_vm_init(struct oak_vm_t* vm, struct oak_allocator_t* allocator)
{
  vm->chunk = null;
  vm->ip = null;
  vm->sp = vm->stack;
  vm->stack_base = 0;
  vm->frame_count = 0;
  vm->modules = null;
  vm->allocator = allocator;
}

void oak_vm_set_module_registry(struct oak_vm_t* vm,
                                struct oak_module_registry_t* modules)
{
  vm->modules = modules;
}

void oak_vm_free(struct oak_vm_t* vm)
{
  while (vm->sp > vm->stack)
  {
    --vm->sp;
    oak_value_decref(*vm->sp);
  }

  vm->chunk = null;
  vm->ip = null;
}

/* Sync cached registers back into the VM struct (before calls, returns, and
 * any helper that reads vm->ip / vm->sp / vm->chunk). */
#define SYNC_TO_VM()                                                           \
  do                                                                           \
  {                                                                            \
    vm->ip = ip;                                                               \
    vm->sp = sp;                                                               \
    vm->chunk = chunk;                                                         \
  } while (0)

/* Reload cached registers from the VM struct (after calls/returns that may
 * have changed them). */
#define SYNC_FROM_VM()                                                         \
  do                                                                           \
  {                                                                            \
    ip = vm->ip;                                                               \
    sp = vm->sp;                                                               \
    chunk = vm->chunk;                                                         \
  } while (0)

/* Validate that a computed local-variable index is within the stack. */
#define CHECK_LOCAL(idx)                                                       \
  do                                                                           \
  {                                                                            \
    if ((idx) >= OAK_STACK_MAX)                                                \
    {                                                                          \
      SYNC_TO_VM();                                                            \
      oak_vm_runtime_error(vm, "local slot out of range (index %zu)",          \
                           (usize)(idx));                                      \
      return OAK_VM_RUNTIME_ERROR;                                             \
    }                                                                          \
  } while (0)

/* Inline read helpers that operate on the cached `ip` local. */
#define READ_U8()  (*ip++)
#define READ_U16() (ip += 2, (u16)(((u16)ip[-2] << 8) | ip[-1]))

/* Inline push that avoids refcount overhead for non-object values (#6). */
#define PUSH_VAL(v)                                                            \
  do                                                                           \
  {                                                                            \
    if (sp >= vm->stack + OAK_STACK_MAX)                                       \
    {                                                                          \
      SYNC_TO_VM();                                                            \
      oak_vm_report_stack_overflow(vm);                                        \
      return OAK_VM_RUNTIME_ERROR;                                             \
    }                                                                          \
    const struct oak_value_t _pv = (v);                                        \
    oak_value_incref(_pv);                                                     \
    *sp++ = _pv;                                                               \
  } while (0)

/* Push a value that needs no incref (immediates or freshly allocated). */
#define PUSH_OWNED(v)                                                          \
  do                                                                           \
  {                                                                            \
    if (sp >= vm->stack + OAK_STACK_MAX)                                       \
    {                                                                          \
      SYNC_TO_VM();                                                            \
      oak_vm_report_stack_overflow(vm);                                        \
      return OAK_VM_RUNTIME_ERROR;                                             \
    }                                                                          \
    *sp++ = (v);                                                               \
  } while (0)

#define POP() (*--sp)

/* Helper: dispatch to vm_object_dispatch / oak_vm_op_call etc. that need the
 * vm struct in sync. Returns from oak_vm_run on error. */
#define DISPATCH_HELPER(call)                                                  \
  do                                                                           \
  {                                                                            \
    SYNC_TO_VM();                                                              \
    const enum oak_vm_result_t _r = (call);                                    \
    if (_r != OAK_VM_OK)                                                       \
      return _r;                                                               \
    SYNC_FROM_VM();                                                            \
  } while (0)

enum oak_vm_result_t oak_vm_run(struct oak_vm_t* vm, struct oak_chunk_t* chunk)
{
  if (chunk->count == 0)
  {
    oak_log(OAK_LOG_ERROR, "vm: empty chunk");
    return OAK_VM_RUNTIME_ERROR;
  }

  vm->chunk = chunk;
  vm->ip = chunk->bytecode;

  /* Cache hot VM registers in locals so the compiler can keep them in CPU
   * registers across iterations (#8). */
  u8* ip = vm->ip;
  struct oak_value_t* sp = vm->sp;

  for (;;)
  {
    const u8 instruction = READ_U8();
    switch (instruction)
    {
      /* ====== HALT ====== */
      case OAK_OP_HALT:
        SYNC_TO_VM();
        return OAK_VM_OK;

      /* ====== CONSTANTS & LITERALS (inlined, #3) ====== */
      case OAK_OP_CONSTANT:
      {
        const u16 idx = READ_U16();
        oak_assert((usize)idx < chunk->const_count);
        PUSH_VAL(chunk->constants[idx]);
        break;
      }
      case OAK_OP_PUSH_INT8:
      {
        const signed char val = (signed char)READ_U8();
        PUSH_OWNED(OAK_VALUE_I32((int)val));
        break;
      }
      case OAK_OP_TRUE:
        PUSH_OWNED(OAK_VALUE_BOOL(1));
        break;
      case OAK_OP_FALSE:
        PUSH_OWNED(OAK_VALUE_BOOL(0));
        break;
      case OAK_OP_NONE:
        PUSH_OWNED(OAK_VALUE_NONE);
        break;

      /* ====== STACK OPS (inlined, #3) ====== */
      case OAK_OP_POP:
      {
        oak_value_decref(POP());
        break;
      }
      case OAK_OP_POP_N:
      {
        const u8 n = READ_U8();
        oak_assert((usize)(sp - vm->stack) >= (usize)n);
        for (u8 i = 0; i < n; ++i)
          oak_value_decref(*--sp);
        break;
      }

      /* ====== LOCAL VARIABLE ACCESS (inlined, #3) ====== */
      case OAK_OP_GET_LOCAL:
      {
        const u8 slot = READ_U8();
        const usize idx = vm->stack_base + (usize)slot;
        CHECK_LOCAL(idx);
        const struct oak_value_t v = vm->stack[idx];
        if (v.tag < OAK_TAG_OBJ)
          PUSH_OWNED(v);
        else
          PUSH_VAL(v);
        break;
      }
      case OAK_OP_SET_LOCAL:
      {
        const u8 slot = READ_U8();
        const usize idx = vm->stack_base + (usize)slot;
        CHECK_LOCAL(idx);
        const struct oak_value_t new_val = POP();
        const struct oak_value_t old_val = vm->stack[idx];
        vm->stack[idx] = new_val;
        oak_value_decref(old_val);
        break;
      }
      case OAK_OP_INC_LOCAL:
      {
        const u8 slot = READ_U8();
        const usize idx = vm->stack_base + (usize)slot;
        CHECK_LOCAL(idx);
        const struct oak_value_t val = vm->stack[idx];
        if (oak_is_i32(val))
        {
          vm->stack[idx] = OAK_VALUE_I32(oak_as_i32(val) + 1);
          break;
        }
        if (oak_is_f32(val))
        {
          vm->stack[idx] = OAK_VALUE_F32(oak_as_f32(val) + 1.0f);
          break;
        }
        SYNC_TO_VM();
        oak_vm_runtime_error(
            vm,
            "local increment/decrement expects a number, got %s",
            oak_vm_value_kind_desc(val));
        return OAK_VM_RUNTIME_ERROR;
      }
      case OAK_OP_DEC_LOCAL:
      {
        const u8 slot = READ_U8();
        const usize idx = vm->stack_base + (usize)slot;
        CHECK_LOCAL(idx);
        const struct oak_value_t val = vm->stack[idx];
        if (oak_is_i32(val))
        {
          vm->stack[idx] = OAK_VALUE_I32(oak_as_i32(val) - 1);
          break;
        }
        if (oak_is_f32(val))
        {
          vm->stack[idx] = OAK_VALUE_F32(oak_as_f32(val) - 1.0f);
          break;
        }
        SYNC_TO_VM();
        oak_vm_runtime_error(
            vm,
            "local increment/decrement expects a number, got %s",
            oak_vm_value_kind_desc(val));
        return OAK_VM_RUNTIME_ERROR;
      }
      case OAK_OP_WEAKEN:
      {
        oak_assert(sp > vm->stack);
        const struct oak_value_t value = sp[-1];
        if (!oak_is_obj(value))
        {
          SYNC_TO_VM();
          oak_vm_runtime_error(
              vm, "weak reference requires an object, got %s",
              oak_vm_value_kind_desc(value));
          return OAK_VM_RUNTIME_ERROR;
        }
        sp[-1] = oak_value_weaken(value);
        oak_refcount_inc(&oak_val_obj_ptr(value)->weak_refcount);
        oak_value_decref(value);
        break;
      }

      /* ====== DEDICATED ARITHMETIC OPCODES (#4) ====== */
      case OAK_OP_ADD:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_string(a) && oak_is_string(b))
        {
          struct oak_obj_string_t* result =
              oak_string_concat(vm->allocator, oak_as_string(a), oak_as_string(b));
          oak_value_decref(a);
          oak_value_decref(b);
          PUSH_OWNED(OAK_VALUE_OBJ(result));
          break;
        }
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          oak_value_decref(a);
          oak_value_decref(b);
          PUSH_OWNED(OAK_VALUE_I32(oak_as_i32(a) + oak_as_i32(b)));
          break;
        }
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_binary(vm, OAK_BINOP_ADD, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }
      case OAK_OP_SUBTRACT:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          oak_value_decref(a);
          oak_value_decref(b);
          PUSH_OWNED(OAK_VALUE_I32(oak_as_i32(a) - oak_as_i32(b)));
          break;
        }
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_binary(vm, OAK_BINOP_SUBTRACT, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }
      case OAK_OP_MULTIPLY:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          oak_value_decref(a);
          oak_value_decref(b);
          PUSH_OWNED(OAK_VALUE_I32(oak_as_i32(a) * oak_as_i32(b)));
          break;
        }
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_binary(vm, OAK_BINOP_MULTIPLY, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }
      case OAK_OP_DIVIDE:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_binary(vm, OAK_BINOP_DIVIDE, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }
      case OAK_OP_INT_DIVIDE:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_binary(vm, OAK_BINOP_INT_DIVIDE, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }
      case OAK_OP_MODULO:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          if (oak_as_i32(b) == 0)
          {
            oak_value_decref(a);
            oak_value_decref(b);
            SYNC_TO_VM();
            oak_vm_runtime_error(
                vm, "integer remainder by zero (modulo by zero)");
            return OAK_VM_RUNTIME_ERROR;
          }
          oak_value_decref(a);
          oak_value_decref(b);
          PUSH_OWNED(OAK_VALUE_I32(oak_as_i32(a) % oak_as_i32(b)));
          break;
        }
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_binary(vm, OAK_BINOP_MODULO, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }

      /* ====== DEDICATED EQUALITY OPCODES (#4) ====== */
      case OAK_OP_EQUAL:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        const int eq = oak_value_equal(a, b);
        oak_value_decref(a);
        oak_value_decref(b);
        PUSH_OWNED(OAK_VALUE_BOOL(eq));
        break;
      }
      case OAK_OP_NOT_EQUAL:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        const int eq = oak_value_equal(a, b);
        oak_value_decref(a);
        oak_value_decref(b);
        PUSH_OWNED(OAK_VALUE_BOOL(!eq));
        break;
      }

      /* ====== DEDICATED COMPARISON OPCODES (#4, #7 integer fast path) ====== */
      case OAK_OP_LESS:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          PUSH_OWNED(OAK_VALUE_BOOL(oak_as_i32(a) < oak_as_i32(b)));
          break;
        }
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_compare(vm, OAK_BINOP_LESS, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }
      case OAK_OP_LESS_EQUAL:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          PUSH_OWNED(OAK_VALUE_BOOL(oak_as_i32(a) <= oak_as_i32(b)));
          break;
        }
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_compare(vm, OAK_BINOP_LESS_EQUAL, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }
      case OAK_OP_GREATER:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          PUSH_OWNED(OAK_VALUE_BOOL(oak_as_i32(a) > oak_as_i32(b)));
          break;
        }
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_compare(vm, OAK_BINOP_GREATER, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }
      case OAK_OP_GREATER_EQUAL:
      {
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          PUSH_OWNED(OAK_VALUE_BOOL(oak_as_i32(a) >= oak_as_i32(b)));
          break;
        }
        SYNC_TO_VM();
        {
          const enum oak_vm_result_t r =
              oak_vm_numeric_compare(vm, OAK_BINOP_GREATER_EQUAL, a, b);
          oak_value_decref(a);
          oak_value_decref(b);
          if (r != OAK_VM_OK)
            return r;
          SYNC_FROM_VM();
        }
        break;
      }

      /* ====== UNARY ====== */
      case OAK_OP_NEGATE:
      {
        struct oak_value_t val = POP();
        if (!oak_is_number(val))
        {
          SYNC_TO_VM();
          oak_value_decref(val);
          oak_vm_runtime_error(vm,
                               "unary '-' expects a number, got %s",
                               oak_vm_value_kind_desc(val));
          return OAK_VM_RUNTIME_ERROR;
        }
        const struct oak_value_t result = oak_is_i32(val)
                                              ? OAK_VALUE_I32(-oak_as_i32(val))
                                              : OAK_VALUE_F32(-oak_as_f32(val));
        oak_value_decref(val);
        PUSH_OWNED(result);
        break;
      }
      case OAK_OP_NOT:
      {
        struct oak_value_t val = POP();
        const struct oak_value_t result = OAK_VALUE_BOOL(!oak_is_truthy(val));
        oak_value_decref(val);
        PUSH_OWNED(result);
        break;
      }
      case OAK_OP_BOOL:
      {
        struct oak_value_t val = POP();
        const struct oak_value_t result = OAK_VALUE_BOOL(oak_is_truthy(val));
        oak_value_decref(val);
        PUSH_OWNED(result);
        break;
      }

      /* ====== CONTROL FLOW (inlined, #3) ====== */
      case OAK_OP_JUMP:
      {
        const u16 offset = READ_U16();
        ip += offset;
        break;
      }
      case OAK_OP_JUMP_IF_FALSE:
      {
        const u16 offset = READ_U16();
        const struct oak_value_t cond = POP();
        if (!oak_is_truthy(cond))
          ip += offset;
        oak_value_decref(cond);
        break;
      }
      case OAK_OP_JUMP_IF_TRUE:
      {
        const u16 offset = READ_U16();
        const struct oak_value_t cond = POP();
        if (oak_is_truthy(cond))
          ip += offset;
        oak_value_decref(cond);
        break;
      }
      case OAK_OP_LOOP:
      {
        const u16 offset = READ_U16();
        ip -= offset;
        break;
      }

      /* ====== FUSED COMPARE+BRANCH (#5) ====== */
      case OAK_OP_LESS_JUMP_IF_FALSE:
      {
        const u16 offset = READ_U16();
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          if (!(oak_as_i32(a) < oak_as_i32(b)))
            ip += offset;
          break;
        }
        if (!(oak_is_number(a) && oak_is_number(b)))
        {
          SYNC_TO_VM();
          oak_value_decref(a);
          oak_value_decref(b);
          oak_vm_runtime_error(vm,
                               "comparison operands must be numbers");
          return OAK_VM_RUNTIME_ERROR;
        }
        {
          const float fa = oak_is_f32(a) ? oak_as_f32(a) : (float)oak_as_i32(a);
          const float fb = oak_is_f32(b) ? oak_as_f32(b) : (float)oak_as_i32(b);
          if (!(fa < fb))
            ip += offset;
        }
        oak_value_decref(a);
        oak_value_decref(b);
        break;
      }
      case OAK_OP_LESS_EQUAL_JUMP_IF_FALSE:
      {
        const u16 offset = READ_U16();
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          if (!(oak_as_i32(a) <= oak_as_i32(b)))
            ip += offset;
          break;
        }
        if (!(oak_is_number(a) && oak_is_number(b)))
        {
          SYNC_TO_VM();
          oak_value_decref(a);
          oak_value_decref(b);
          oak_vm_runtime_error(vm,
                               "comparison operands must be numbers");
          return OAK_VM_RUNTIME_ERROR;
        }
        {
          const float fa = oak_is_f32(a) ? oak_as_f32(a) : (float)oak_as_i32(a);
          const float fb = oak_is_f32(b) ? oak_as_f32(b) : (float)oak_as_i32(b);
          if (!(fa <= fb))
            ip += offset;
        }
        oak_value_decref(a);
        oak_value_decref(b);
        break;
      }
      case OAK_OP_GREATER_JUMP_IF_FALSE:
      {
        const u16 offset = READ_U16();
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          if (!(oak_as_i32(a) > oak_as_i32(b)))
            ip += offset;
          break;
        }
        if (!(oak_is_number(a) && oak_is_number(b)))
        {
          SYNC_TO_VM();
          oak_value_decref(a);
          oak_value_decref(b);
          oak_vm_runtime_error(vm,
                               "comparison operands must be numbers");
          return OAK_VM_RUNTIME_ERROR;
        }
        {
          const float fa = oak_is_f32(a) ? oak_as_f32(a) : (float)oak_as_i32(a);
          const float fb = oak_is_f32(b) ? oak_as_f32(b) : (float)oak_as_i32(b);
          if (!(fa > fb))
            ip += offset;
        }
        oak_value_decref(a);
        oak_value_decref(b);
        break;
      }
      case OAK_OP_GREATER_EQUAL_JUMP_IF_FALSE:
      {
        const u16 offset = READ_U16();
        const struct oak_value_t b = POP();
        const struct oak_value_t a = POP();
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          if (!(oak_as_i32(a) >= oak_as_i32(b)))
            ip += offset;
          break;
        }
        if (!(oak_is_number(a) && oak_is_number(b)))
        {
          SYNC_TO_VM();
          oak_value_decref(a);
          oak_value_decref(b);
          oak_vm_runtime_error(vm,
                               "comparison operands must be numbers");
          return OAK_VM_RUNTIME_ERROR;
        }
        {
          const float fa = oak_is_f32(a) ? oak_as_f32(a) : (float)oak_as_i32(a);
          const float fb = oak_is_f32(b) ? oak_as_f32(b) : (float)oak_as_i32(b);
          if (!(fa >= fb))
            ip += offset;
        }
        oak_value_decref(a);
        oak_value_decref(b);
        break;
      }

      /* ====== SUPERINSTRUCTIONS (#9) ====== */
      case OAK_OP_GET_LOCAL_GET_LOCAL:
      {
        const u8 slot1 = READ_U8();
        const u8 slot2 = READ_U8();
        const usize idx1 = vm->stack_base + (usize)slot1;
        const usize idx2 = vm->stack_base + (usize)slot2;
        CHECK_LOCAL(idx1);
        CHECK_LOCAL(idx2);
        PUSH_VAL(vm->stack[idx1]);
        PUSH_VAL(vm->stack[idx2]);
        break;
      }
      case OAK_OP_INC_LOCAL_LOOP:
      {
        const u8 slot = READ_U8();
        const u16 offset = READ_U16();
        const usize idx = vm->stack_base + (usize)slot;
        CHECK_LOCAL(idx);
        const struct oak_value_t val = vm->stack[idx];
        if (oak_is_i32(val))
        {
          vm->stack[idx] = OAK_VALUE_I32(oak_as_i32(val) + 1);
          ip -= offset;
          break;
        }
        if (oak_is_f32(val))
        {
          vm->stack[idx] = OAK_VALUE_F32(oak_as_f32(val) + 1.0f);
          ip -= offset;
          break;
        }
        SYNC_TO_VM();
        oak_vm_runtime_error(
            vm,
            "local increment/decrement expects a number, got %s",
            oak_vm_value_kind_desc(val));
        return OAK_VM_RUNTIME_ERROR;
      }

      /* ====== CALLS & RETURNS ====== */
      case OAK_OP_CALL:
      {
        const u8 argc = READ_U8();
        const usize depth = (usize)(sp - vm->stack);
        if (depth < (usize)argc + 1u)
        {
          SYNC_TO_VM();
          oak_vm_runtime_error(vm, "stack underflow in call");
          return OAK_VM_RUNTIME_ERROR;
        }
        const usize fn_slot = depth - (usize)argc - 1u;
        const struct oak_value_t fn_val = vm->stack[fn_slot];

        if (fn_val.tag == OAK_TAG_OBJ &&
            fn_val.as.obj->type == OAK_OBJ_FN)
        {
          struct oak_obj_fn_t* fn = (struct oak_obj_fn_t*)fn_val.as.obj;
          if (fn->arity == (int)argc &&
              fn->attr_hook_count == 0 &&
              vm->frame_count < OAK_FRAMES_MAX &&
              (fn->module_id == 0xFFFFu ||
               fn->module_id == chunk->module_id))
          {
            struct oak_call_frame_t* frame =
                &vm->frames[vm->frame_count++];
            frame->return_ip = ip;
            frame->caller_stack_base = vm->stack_base;
            frame->fn_slot = fn_slot;
            frame->return_chunk = chunk;
            vm->stack_base = fn_slot + 1u;
            ip = chunk->bytecode + fn->code_offset;
            break;
          }
        }

        SYNC_TO_VM();
        {
          const enum oak_vm_result_t _r =
              oak_vm_op_call_with_argc(vm, argc);
          if (_r != OAK_VM_OK)
            return _r;
        }
        SYNC_FROM_VM();
        break;
      }
      case OAK_OP_RETURN:
      {
        if (vm->frame_count == 0)
        {
          SYNC_TO_VM();
          oak_vm_runtime_error(vm, "'return' outside of a function");
          return OAK_VM_RUNTIME_ERROR;
        }
        if (sp <= vm->stack)
        {
          SYNC_TO_VM();
          oak_vm_runtime_error(vm, "stack underflow in return");
          return OAK_VM_RUNTIME_ERROR;
        }

        const struct oak_value_t result = *--sp;
        struct oak_call_frame_t* frame =
            &vm->frames[--vm->frame_count];
        const usize fn_slot = frame->fn_slot;
        const usize depth_before = (usize)(sp - vm->stack) + 1u;

        for (usize i = fn_slot; i < depth_before - 1u; ++i)
        {
          const struct oak_value_t v = vm->stack[i];
          if (v.tag == OAK_TAG_OBJ)
            oak_obj_decref(v.as.obj);
          else if (v.tag == OAK_TAG_WEAK)
            oak_weak_decref(v.as.obj);
        }

        vm->stack[fn_slot] = result;
        sp = vm->stack + fn_slot + 1u;
        ip = frame->return_ip;
        vm->stack_base = frame->caller_stack_base;
        chunk = frame->return_chunk;
        break;
      }
      case OAK_OP_CALL_VIRTUAL:
        DISPATCH_HELPER(oak_vm_op_call_virtual(vm));
        break;

      /* ====== OBJECTS (delegated to vm_object_dispatch) ====== */
      case OAK_OP_NEW_ARR:
      case OAK_OP_NEW_MAP:
      case OAK_OP_GET_INDEX:
      case OAK_OP_SET_INDEX:
      case OAK_OP_MAP_KEY_AT:
      case OAK_OP_MAP_VAL_AT:
      case OAK_OP_NEW_RECORD:
      case OAK_OP_GET_FIELD:
      case OAK_OP_SET_FIELD:
      case OAK_OP_GET_MODULE_FN:
      case OAK_OP_MAKE_TRAIT_OBJECT:
        DISPATCH_HELPER(vm_object_dispatch(vm, chunk, instruction));
        break;

      default:
        SYNC_TO_VM();
        oak_vm_runtime_error(
            vm, "internal error: unknown opcode 0x%02x", instruction);
        return OAK_VM_RUNTIME_ERROR;
    }
  }
}

