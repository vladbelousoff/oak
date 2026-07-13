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
  vm->user_data = null;
  vm->debug_hook = null;
}

void oak_vm_set_debug_hook(struct oak_vm_t* vm,
                           struct oak_vm_debug_hook_t* hook)
{
  vm->debug_hook = hook;
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

static void cached_sync_to_vm(struct oak_vm_t* vm,
                              struct oak_chunk_t* chunk,
                              u8* ip,
                              struct oak_value_t* sp)
{
  vm->chunk = chunk;
  vm->ip = ip;
  vm->sp = sp;
}

static void cached_sync_from_vm(struct oak_vm_t* vm,
                                struct oak_chunk_t** chunk,
                                u8** ip,
                                struct oak_value_t** sp)
{
  *chunk = vm->chunk;
  *ip = vm->ip;
  *sp = vm->sp;
}

static enum oak_vm_result_t cached_push(struct oak_vm_t* vm,
                                        struct oak_chunk_t* chunk,
                                        u8* ip,
                                        struct oak_value_t** sp,
                                        struct oak_value_t value,
                                        int owned)
{
  if (*sp >= vm->stack + OAK_STACK_MAX)
  {
    cached_sync_to_vm(vm, chunk, ip, *sp);
    if (owned)
      oak_value_decref(value);
    oak_vm_report_stack_overflow(vm);
    return OAK_VM_RUNTIME_ERROR;
  }
  if (!owned)
    oak_value_incref(value);
  *(*sp)++ = value;
  return OAK_VM_OK;
}

static enum oak_vm_result_t cached_push_value(struct oak_vm_t* vm,
                                              struct oak_chunk_t* chunk,
                                              u8* ip,
                                              struct oak_value_t** sp,
                                              struct oak_value_t value)
{
  return cached_push(vm, chunk, ip, sp, value, 0);
}

static enum oak_vm_result_t cached_push_owned(struct oak_vm_t* vm,
                                              struct oak_chunk_t* chunk,
                                              u8* ip,
                                              struct oak_value_t** sp,
                                              struct oak_value_t value)
{
  return cached_push(vm, chunk, ip, sp, value, 1);
}

static int cached_local_is_valid(struct oak_vm_t* vm,
                                 struct oak_chunk_t* chunk,
                                 u8* ip,
                                 struct oak_value_t* sp,
                                 usize idx)
{
  if (idx < OAK_STACK_MAX)
    return 1;
  cached_sync_to_vm(vm, chunk, ip, sp);
  oak_vm_runtime_error(vm, "local slot out of range (index %zu)", idx);
  return 0;
}

static enum oak_vm_result_t numeric_slow_path(
    struct oak_vm_t* vm,
    struct oak_chunk_t** chunk,
    u8** ip,
    struct oak_value_t** sp,
    enum oak_binop_t binop,
    struct oak_value_t a,
    struct oak_value_t b)
{
  cached_sync_to_vm(vm, *chunk, *ip, *sp);
  const enum oak_vm_result_t result = oak_vm_numeric_binary(vm, binop, a, b);
  oak_value_decref(a);
  oak_value_decref(b);
  cached_sync_from_vm(vm, chunk, ip, sp);
  return result;
}

static struct oak_value_t equality_result(struct oak_value_t a,
                                          struct oak_value_t b,
                                          int negate)
{
  const int equal = oak_value_equal(a, b);
  oak_value_decref(a);
  oak_value_decref(b);
  return OAK_VALUE_BOOL(negate ? !equal : equal);
}

static u8 cached_read_u8(u8** ip)
{
  return *(*ip)++;
}

static u16 cached_read_u16(u8** ip)
{
  const u16 hi = cached_read_u8(ip);
  const u16 lo = cached_read_u8(ip);
  return (u16)((hi << 8) | lo);
}

static struct oak_value_t cached_pop(struct oak_value_t** sp)
{
  return *--*sp;
}

static int update_numeric_local(struct oak_value_t* value, int delta)
{
  if (oak_is_i32(*value))
  {
    *value = OAK_VALUE_I32(oak_i32_wrap_add(oak_as_i32(*value), delta));
    return 1;
  }
  if (oak_is_f32(*value))
  {
    *value = OAK_VALUE_F32(oak_as_f32(*value) + (float)delta);
    return 1;
  }
  return 0;
}

static int compare_numeric_values(struct oak_value_t a,
                                  struct oak_value_t b,
                                  enum oak_binop_t binop,
                                  int* result)
{
  if (!(oak_is_number(a) && oak_is_number(b)))
    return 0;

  if (oak_is_i32(a) && oak_is_i32(b))
  {
    const int left = oak_as_i32(a);
    const int right = oak_as_i32(b);
    switch (binop)
    {
      case OAK_BINOP_LESS:          *result = left < right;  return 1;
      case OAK_BINOP_LESS_EQUAL:    *result = left <= right; return 1;
      case OAK_BINOP_GREATER:       *result = left > right;  return 1;
      case OAK_BINOP_GREATER_EQUAL: *result = left >= right; return 1;
      default: oak_panic("invalid integer comparison binop");
    }
  }

  const float left =
      oak_is_f32(a) ? oak_as_f32(a) : (float)oak_as_i32(a);
  const float right =
      oak_is_f32(b) ? oak_as_f32(b) : (float)oak_as_i32(b);
  switch (binop)
  {
    case OAK_BINOP_LESS:          *result = left < right;  return 1;
    case OAK_BINOP_LESS_EQUAL:    *result = left <= right; return 1;
    case OAK_BINOP_GREATER:       *result = left > right;  return 1;
    case OAK_BINOP_GREATER_EQUAL: *result = left >= right; return 1;
    default: oak_panic("invalid numeric comparison binop");
  }
}

static enum oak_binop_t comparison_binop(u8 instruction)
{
  switch (instruction)
  {
    case OAK_OP_LESS:
    case OAK_OP_LESS_JUMP_IF_FALSE:
      return OAK_BINOP_LESS;
    case OAK_OP_LESS_EQUAL:
    case OAK_OP_LESS_EQUAL_JUMP_IF_FALSE:
      return OAK_BINOP_LESS_EQUAL;
    case OAK_OP_GREATER:
    case OAK_OP_GREATER_JUMP_IF_FALSE:
      return OAK_BINOP_GREATER;
    case OAK_OP_GREATER_EQUAL:
    case OAK_OP_GREATER_EQUAL_JUMP_IF_FALSE:
      return OAK_BINOP_GREATER_EQUAL;
    default:
      oak_panic("invalid comparison opcode");
  }
}

enum oak_vm_result_t oak_vm_run(struct oak_vm_t* vm, struct oak_chunk_t* chunk)
{
  if (chunk->count == 0)
  {
    oak_log(OAK_LOG_ERROR, "vm: empty chunk");
    return OAK_VM_RUNTIME_ERROR;
  }

  vm->chunk = chunk;
  vm->ip = chunk->bytecode;
  return oak_vm_resume(vm);
}

enum oak_vm_result_t oak_vm_resume(struct oak_vm_t* vm)
{
  if (!vm->chunk || !vm->ip)
  {
    oak_log(OAK_LOG_ERROR, "vm: no active chunk");
    return OAK_VM_RUNTIME_ERROR;
  }

  struct oak_chunk_t* chunk = vm->chunk;
  /* Cache hot VM registers in locals so the compiler can keep them in CPU
   * registers across iterations (#8). */
  u8* ip = vm->ip;
  struct oak_value_t* sp = vm->sp;

  for (;;)
  {
    if (vm->debug_hook)
    {
      cached_sync_to_vm(vm, chunk, ip, sp);
      const enum oak_debug_action_t dbg_action =
          vm->debug_hook->fn(vm, vm->debug_hook->ctx);
      cached_sync_from_vm(vm, &chunk, &ip, &sp);
      if (dbg_action == OAK_DEBUG_HALT)
        return OAK_VM_DEBUG_HALT;
    }
    const u8 instruction = cached_read_u8(&ip);
    switch (instruction)
    {
      /* ====== HALT ====== */
      case OAK_OP_HALT:
        cached_sync_to_vm(vm, chunk, ip, sp);
        return OAK_VM_OK;

      /* ====== CONSTANTS & LITERALS (inlined, #3) ====== */
      case OAK_OP_CONSTANT:
      {
        const u16 idx = cached_read_u16(&ip);
        oak_assert((usize)idx < chunk->const_count);
        if (cached_push_value(vm, chunk, ip, &sp, chunk->constants[idx]) !=
            OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }
      case OAK_OP_PUSH_INT8:
      {
        const signed char val = (signed char)cached_read_u8(&ip);
        if (cached_push_owned(vm, chunk, ip, &sp, OAK_VALUE_I32((int)val)) !=
            OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }
      case OAK_OP_TRUE:
        if (cached_push_owned(vm, chunk, ip, &sp, OAK_VALUE_BOOL(1)) !=
            OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      case OAK_OP_FALSE:
        if (cached_push_owned(vm, chunk, ip, &sp, OAK_VALUE_BOOL(0)) !=
            OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      case OAK_OP_NONE:
        if (cached_push_owned(vm, chunk, ip, &sp, OAK_VALUE_NONE) != OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;

      /* ====== STACK OPS (inlined, #3) ====== */
      case OAK_OP_POP:
      {
        oak_value_decref(cached_pop(&sp));
        break;
      }
      case OAK_OP_POP_N:
      {
        const u8 n = cached_read_u8(&ip);
        oak_assert((usize)(sp - vm->stack) >= (usize)n);
        for (u8 i = 0; i < n; ++i)
          oak_value_decref(*--sp);
        break;
      }

      /* ====== LOCAL VARIABLE ACCESS (inlined, #3) ====== */
      case OAK_OP_GET_LOCAL:
      {
        const u8 slot = cached_read_u8(&ip);
        const usize idx = vm->stack_base + (usize)slot;
        if (!cached_local_is_valid(vm, chunk, ip, sp, idx))
          return OAK_VM_RUNTIME_ERROR;
        const struct oak_value_t v = vm->stack[idx];
        if (oak_value_tag(v) != OAK_TAG_OBJ)
        {
          if (cached_push_owned(vm, chunk, ip, &sp, v) != OAK_VM_OK)
            return OAK_VM_RUNTIME_ERROR;
        }
        else if (cached_push_value(vm, chunk, ip, &sp, v) != OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }
      case OAK_OP_SET_LOCAL:
      {
        const u8 slot = cached_read_u8(&ip);
        const usize idx = vm->stack_base + (usize)slot;
        if (!cached_local_is_valid(vm, chunk, ip, sp, idx))
          return OAK_VM_RUNTIME_ERROR;
        const struct oak_value_t new_val = cached_pop(&sp);
        const struct oak_value_t old_val = vm->stack[idx];
        vm->stack[idx] = new_val;
        oak_value_decref(old_val);
        break;
      }
      case OAK_OP_INC_LOCAL:
      case OAK_OP_DEC_LOCAL:
      {
        const int delta = instruction == OAK_OP_INC_LOCAL ? 1 : -1;
        const u8 slot = cached_read_u8(&ip);
        const usize idx = vm->stack_base + (usize)slot;
        if (!cached_local_is_valid(vm, chunk, ip, sp, idx))
          return OAK_VM_RUNTIME_ERROR;
        if (update_numeric_local(&vm->stack[idx], delta))
          break;
        cached_sync_to_vm(vm, chunk, ip, sp);
        oak_vm_runtime_error(
            vm,
            "local increment/decrement expects a number, got %s",
            oak_vm_value_kind_desc(vm->stack[idx]));
        return OAK_VM_RUNTIME_ERROR;
      }
      case OAK_OP_WEAKEN:
      {
        oak_assert(sp > vm->stack);
        const struct oak_value_t value = sp[-1];
        if (!oak_is_obj(value))
        {
          cached_sync_to_vm(vm, chunk, ip, sp);
          oak_vm_runtime_error(
              vm, "weak reference requires an object, got %s",
              oak_vm_value_kind_desc(value));
          return OAK_VM_RUNTIME_ERROR;
        }
        /* The weak copy carries no refcount; dropping the strong reference
         * may free the object here, leaving an already-expired weak. */
        sp[-1] = oak_value_weaken(value);
        oak_value_decref(value);
        break;
      }

      /* ====== DEDICATED ARITHMETIC OPCODES (#4) ====== */
      case OAK_OP_ADD:
      {
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        if (oak_is_string(a) && oak_is_string(b))
        {
          struct oak_obj_string_t* result =
              oak_string_concat(vm->allocator, oak_as_string(a), oak_as_string(b));
          oak_value_decref(a);
          oak_value_decref(b);
          if (cached_push_owned(
                  vm, chunk, ip, &sp, OAK_VALUE_OBJ(result)) != OAK_VM_OK)
            return OAK_VM_RUNTIME_ERROR;
          break;
        }
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          oak_value_decref(a);
          oak_value_decref(b);
          if (cached_push_owned(
                  vm,
                  chunk,
                  ip,
                  &sp,
                  OAK_VALUE_I32(
                      oak_i32_wrap_add(oak_as_i32(a), oak_as_i32(b)))) !=
              OAK_VM_OK)
            return OAK_VM_RUNTIME_ERROR;
          break;
        }
        const enum oak_vm_result_t r =
            numeric_slow_path(vm, &chunk, &ip, &sp, OAK_BINOP_ADD, a, b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_SUBTRACT:
      {
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          oak_value_decref(a);
          oak_value_decref(b);
          if (cached_push_owned(
                  vm,
                  chunk,
                  ip,
                  &sp,
                  OAK_VALUE_I32(
                      oak_i32_wrap_sub(oak_as_i32(a), oak_as_i32(b)))) !=
              OAK_VM_OK)
            return OAK_VM_RUNTIME_ERROR;
          break;
        }
        const enum oak_vm_result_t r =
            numeric_slow_path(vm, &chunk, &ip, &sp, OAK_BINOP_SUBTRACT, a, b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_MULTIPLY:
      {
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          oak_value_decref(a);
          oak_value_decref(b);
          if (cached_push_owned(
                  vm,
                  chunk,
                  ip,
                  &sp,
                  OAK_VALUE_I32(
                      oak_i32_wrap_mul(oak_as_i32(a), oak_as_i32(b)))) !=
              OAK_VM_OK)
            return OAK_VM_RUNTIME_ERROR;
          break;
        }
        const enum oak_vm_result_t r =
            numeric_slow_path(vm, &chunk, &ip, &sp, OAK_BINOP_MULTIPLY, a, b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_DIVIDE:
      {
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        const enum oak_vm_result_t r =
            numeric_slow_path(vm, &chunk, &ip, &sp, OAK_BINOP_DIVIDE, a, b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_INT_DIVIDE:
      {
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        const enum oak_vm_result_t r = numeric_slow_path(
            vm, &chunk, &ip, &sp, OAK_BINOP_INT_DIVIDE, a, b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_MODULO:
      {
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        if (oak_is_i32(a) && oak_is_i32(b))
        {
          if (oak_as_i32(b) == 0)
          {
            oak_value_decref(a);
            oak_value_decref(b);
            cached_sync_to_vm(vm, chunk, ip, sp);
            oak_vm_runtime_error(
                vm, "integer remainder by zero (modulo by zero)");
            return OAK_VM_RUNTIME_ERROR;
          }
          oak_value_decref(a);
          oak_value_decref(b);
          /* INT_MIN % -1 is mathematically 0 but traps on x86. */
          const struct oak_value_t result = OAK_VALUE_I32(
              oak_as_i32(b) == -1 ? 0 : oak_as_i32(a) % oak_as_i32(b));
          if (cached_push_owned(vm, chunk, ip, &sp, result) != OAK_VM_OK)
            return OAK_VM_RUNTIME_ERROR;
          break;
        }
        const enum oak_vm_result_t r =
            numeric_slow_path(vm, &chunk, &ip, &sp, OAK_BINOP_MODULO, a, b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }

      /* ====== DEDICATED EQUALITY OPCODES (#4) ====== */
      case OAK_OP_EQUAL:
      {
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        if (cached_push_owned(
                vm, chunk, ip, &sp, equality_result(a, b, 0)) != OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }
      case OAK_OP_NOT_EQUAL:
      {
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        if (cached_push_owned(
                vm, chunk, ip, &sp, equality_result(a, b, 1)) != OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }

      /* ====== DEDICATED COMPARISON OPCODES (#4, #7 integer fast path) ====== */
      case OAK_OP_LESS:
      case OAK_OP_LESS_EQUAL:
      case OAK_OP_GREATER:
      case OAK_OP_GREATER_EQUAL:
      {
        const enum oak_binop_t binop = comparison_binop(instruction);
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        int result;
        if (!compare_numeric_values(a, b, binop, &result))
        {
          cached_sync_to_vm(vm, chunk, ip, sp);
          oak_vm_runtime_error(
              vm,
              "comparison operands must be numbers (left operand is %s, "
              "right operand is %s)",
              oak_vm_value_kind_desc(a),
              oak_vm_value_kind_desc(b));
          oak_value_decref(a);
          oak_value_decref(b);
          return OAK_VM_RUNTIME_ERROR;
        }
        oak_value_decref(a);
        oak_value_decref(b);
        if (cached_push_owned(vm, chunk, ip, &sp, OAK_VALUE_BOOL(result)) !=
            OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }

      /* ====== UNARY ====== */
      case OAK_OP_NEGATE:
      {
        struct oak_value_t val = cached_pop(&sp);
        if (!oak_is_number(val))
        {
          cached_sync_to_vm(vm, chunk, ip, sp);
          oak_vm_runtime_error(vm,
                               "unary '-' expects a number, got %s",
                               oak_vm_value_kind_desc(val));
          oak_value_decref(val);
          return OAK_VM_RUNTIME_ERROR;
        }
        const struct oak_value_t result =
            oak_is_i32(val) ? OAK_VALUE_I32(oak_i32_wrap_neg(oak_as_i32(val)))
                            : OAK_VALUE_F32(-oak_as_f32(val));
        oak_value_decref(val);
        if (cached_push_owned(vm, chunk, ip, &sp, result) != OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }
      case OAK_OP_NOT:
      {
        struct oak_value_t val = cached_pop(&sp);
        const struct oak_value_t result = OAK_VALUE_BOOL(!oak_is_truthy(val));
        oak_value_decref(val);
        if (cached_push_owned(vm, chunk, ip, &sp, result) != OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }
      case OAK_OP_BOOL:
      {
        struct oak_value_t val = cached_pop(&sp);
        const struct oak_value_t result = OAK_VALUE_BOOL(oak_is_truthy(val));
        oak_value_decref(val);
        if (cached_push_owned(vm, chunk, ip, &sp, result) != OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }

      /* ====== CONTROL FLOW (inlined, #3) ====== */
      case OAK_OP_JUMP:
      {
        const u16 offset = cached_read_u16(&ip);
        ip += offset;
        break;
      }
      case OAK_OP_JUMP_IF_FALSE:
      {
        const u16 offset = cached_read_u16(&ip);
        const struct oak_value_t cond = cached_pop(&sp);
        if (!oak_is_truthy(cond))
          ip += offset;
        oak_value_decref(cond);
        break;
      }
      case OAK_OP_JUMP_IF_TRUE:
      {
        const u16 offset = cached_read_u16(&ip);
        const struct oak_value_t cond = cached_pop(&sp);
        if (oak_is_truthy(cond))
          ip += offset;
        oak_value_decref(cond);
        break;
      }
      case OAK_OP_LOOP:
      {
        const u16 offset = cached_read_u16(&ip);
        ip -= offset;
        break;
      }

      /* ====== FUSED COMPARE+BRANCH (#5) ====== */
      case OAK_OP_LESS_JUMP_IF_FALSE:
      case OAK_OP_LESS_EQUAL_JUMP_IF_FALSE:
      case OAK_OP_GREATER_JUMP_IF_FALSE:
      case OAK_OP_GREATER_EQUAL_JUMP_IF_FALSE:
      {
        const enum oak_binop_t binop = comparison_binop(instruction);
        const u16 offset = cached_read_u16(&ip);
        const struct oak_value_t b = cached_pop(&sp);
        const struct oak_value_t a = cached_pop(&sp);
        int result;
        if (!compare_numeric_values(a, b, binop, &result))
        {
          cached_sync_to_vm(vm, chunk, ip, sp);
          oak_value_decref(a);
          oak_value_decref(b);
          oak_vm_runtime_error(vm, "comparison operands must be numbers");
          return OAK_VM_RUNTIME_ERROR;
        }
        if (!result)
          ip += offset;
        oak_value_decref(a);
        oak_value_decref(b);
        break;
      }

      /* ====== SUPERINSTRUCTIONS (#9) ====== */
      case OAK_OP_GET_LOCAL_GET_LOCAL:
      {
        const u8 slot1 = cached_read_u8(&ip);
        const u8 slot2 = cached_read_u8(&ip);
        const usize idx1 = vm->stack_base + (usize)slot1;
        const usize idx2 = vm->stack_base + (usize)slot2;
        if (!cached_local_is_valid(vm, chunk, ip, sp, idx1) ||
            !cached_local_is_valid(vm, chunk, ip, sp, idx2))
          return OAK_VM_RUNTIME_ERROR;
        if (cached_push_value(vm, chunk, ip, &sp, vm->stack[idx1]) != OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        if (cached_push_value(vm, chunk, ip, &sp, vm->stack[idx2]) != OAK_VM_OK)
          return OAK_VM_RUNTIME_ERROR;
        break;
      }
      case OAK_OP_INC_LOCAL_LOOP:
      {
        const u8 slot = cached_read_u8(&ip);
        const u16 offset = cached_read_u16(&ip);
        const usize idx = vm->stack_base + (usize)slot;
        if (!cached_local_is_valid(vm, chunk, ip, sp, idx))
          return OAK_VM_RUNTIME_ERROR;
        if (update_numeric_local(&vm->stack[idx], 1))
        {
          ip -= offset;
          break;
        }
        cached_sync_to_vm(vm, chunk, ip, sp);
        oak_vm_runtime_error(
            vm,
            "local increment/decrement expects a number, got %s",
            oak_vm_value_kind_desc(vm->stack[idx]));
        return OAK_VM_RUNTIME_ERROR;
      }

      /* ====== CALLS & RETURNS ====== */
      case OAK_OP_CALL:
      {
        const u8 argc = cached_read_u8(&ip);
        const usize depth = (usize)(sp - vm->stack);
        if (depth < (usize)argc + 1u)
        {
          cached_sync_to_vm(vm, chunk, ip, sp);
          oak_vm_runtime_error(vm, "stack underflow in call");
          return OAK_VM_RUNTIME_ERROR;
        }
        const usize fn_slot = depth - (usize)argc - 1u;
        const struct oak_value_t fn_val = vm->stack[fn_slot];

        if (oak_value_tag(fn_val) == OAK_TAG_OBJ &&
            oak_val_obj_ptr(fn_val)->type == OAK_OBJ_FN)
        {
          struct oak_obj_fn_t* fn =
              (struct oak_obj_fn_t*)oak_val_obj_ptr(fn_val);
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

        cached_sync_to_vm(vm, chunk, ip, sp);
        {
          const enum oak_vm_result_t _r =
              oak_vm_op_call_with_argc(vm, argc);
          if (_r != OAK_VM_OK)
            return _r;
        }
        cached_sync_from_vm(vm, &chunk, &ip, &sp);
        break;
      }
      case OAK_OP_RETURN:
      {
        if (vm->frame_count == 0)
        {
          cached_sync_to_vm(vm, chunk, ip, sp);
          oak_vm_runtime_error(vm, "'return' outside of a function");
          return OAK_VM_RUNTIME_ERROR;
        }
        if (sp <= vm->stack)
        {
          cached_sync_to_vm(vm, chunk, ip, sp);
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
          if (oak_value_tag(v) == OAK_TAG_OBJ)
            oak_obj_decref(oak_val_obj_ptr(v));
        }

        vm->stack[fn_slot] = result;
        sp = vm->stack + fn_slot + 1u;
        ip = frame->return_ip;
        vm->stack_base = frame->caller_stack_base;
        chunk = frame->return_chunk;
        break;
      }
      case OAK_OP_CALL_VIRTUAL:
      {
        cached_sync_to_vm(vm, chunk, ip, sp);
        const enum oak_vm_result_t result = oak_vm_op_call_virtual(vm);
        if (result != OAK_VM_OK)
          return result;
        cached_sync_from_vm(vm, &chunk, &ip, &sp);
        break;
      }

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
      {
        cached_sync_to_vm(vm, chunk, ip, sp);
        const enum oak_vm_result_t result =
            vm_object_dispatch(vm, chunk, instruction);
        if (result != OAK_VM_OK)
          return result;
        cached_sync_from_vm(vm, &chunk, &ip, &sp);
        break;
      }

      default:
        cached_sync_to_vm(vm, chunk, ip, sp);
        oak_vm_runtime_error(
            vm, "internal error: unknown opcode 0x%02x", instruction);
        return OAK_VM_RUNTIME_ERROR;
    }
  }
}
