#include "oak_vm.h"

#include <stdarg.h>
#include <stdio.h>

void oak_vm_init(struct oak_vm_t* vm)
{
  vm->chunk = null;
  vm->ip = null;
  vm->sp = vm->stack;
  vm->stack_base = 0;
  vm->frame_count = 0;
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

static void vm_push(struct oak_vm_t* vm, const struct oak_value_t value)
{
  oak_assert(vm->sp < vm->stack + OAK_STACK_MAX);
  oak_value_incref(value);
  *vm->sp++ = value;
}

static struct oak_value_t vm_pop(struct oak_vm_t* vm)
{
  oak_assert(vm->sp > vm->stack);
  return *--vm->sp;
}

static struct oak_value_t vm_peek(const struct oak_vm_t* vm, const int distance)
{
  return vm->sp[-1 - distance];
}

static const char* value_kind_desc(const struct oak_value_t v)
{
  if (oak_is_bool(v))
    return "bool";
  if (oak_is_number(v))
    return oak_is_f32(v) ? "float" : "integer";
  if (oak_is_string(v))
    return "string";
  if (oak_is_fn(v))
    return "function";
  if (oak_is_native_fn(v))
    return "native function";
  if (oak_is_obj(v))
    return "object";
  return "value";
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void runtime_error(const struct oak_vm_t* vm, const char* fmt, ...)
{
  static _Thread_local char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  const usize offset = (usize)(vm->ip - vm->chunk->bytecode - 1);
  oak_assert(vm->chunk->locations != null);
  const struct oak_code_loc_t loc = vm->chunk->locations[offset];
  int col = loc.column;
  if (col < 1)
    col = 1;

  oak_log(OAK_LOG_ERR, "%d:%d: error: %s", loc.line, col, buf);
}

static inline u16 vm_read(struct oak_vm_t* vm, const int n)
{
  u16 val = 0;
  for (int i = 0; i < n; i++)
    val = (val << 8) | *vm->ip++;
  return val;
}

static inline float coerce_f32(const struct oak_value_t v)
{
  return oak_is_f32(v) ? oak_as_f32(v) : (float)oak_as_i32(v);
}

static enum oak_vm_result_t numeric_binary(struct oak_vm_t* vm,
                                           const u8 op,
                                           const struct oak_value_t a,
                                           const struct oak_value_t b)
{
  if (oak_is_i32(a) && oak_is_i32(b))
  {
    int result;
    switch (op)
    {
      case OAK_OP_ADD:
        result = oak_as_i32(a) + oak_as_i32(b);
        break;
      case OAK_OP_SUB:
        result = oak_as_i32(a) - oak_as_i32(b);
        break;
      case OAK_OP_MUL:
        result = oak_as_i32(a) * oak_as_i32(b);
        break;
      case OAK_OP_DIV:
        if (oak_as_i32(b) == 0)
        {
          runtime_error(vm, "integer division by zero");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = oak_as_i32(a) / oak_as_i32(b);
        break;
      case OAK_OP_MOD:
        if (oak_as_i32(b) == 0)
        {
          runtime_error(vm, "integer remainder by zero (modulo by zero)");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = oak_as_i32(a) % oak_as_i32(b);
        break;
      default:
        runtime_error(
            vm, "internal error: unhandled integer opcode (0x%02x)", op);
        return OAK_VM_RUNTIME_ERROR;
    }
    vm_push(vm, OAK_VALUE_I32(result));
    return OAK_VM_OK;
  }

  if (oak_is_number(a) && oak_is_number(b))
  {
    const float fa = coerce_f32(a);
    const float fb = coerce_f32(b);

    float result;
    switch (op)
    {
      case OAK_OP_ADD:
        result = fa + fb;
        break;
      case OAK_OP_SUB:
        result = fa - fb;
        break;
      case OAK_OP_MUL:
        result = fa * fb;
        break;
      case OAK_OP_DIV:
        if (fb == 0.0f)
        {
          runtime_error(vm, "floating-point division by zero");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = fa / fb;
        break;
      case OAK_OP_MOD:
        runtime_error(vm,
                      "the '%%' operator is only defined for integers, not "
                      "floating-point "
                      "operands");
        return OAK_VM_RUNTIME_ERROR;
      default:
        runtime_error(
            vm, "internal error: unhandled numeric opcode (0x%02x)", op);
        return OAK_VM_RUNTIME_ERROR;
    }

    vm_push(vm, OAK_VALUE_F32(result));
    return OAK_VM_OK;
  }

  runtime_error(
      vm,
      "arithmetic operands must be numbers (left operand is %s, right "
      "operand is %s)",
      value_kind_desc(a),
      value_kind_desc(b));
  return OAK_VM_RUNTIME_ERROR;
}

static enum oak_vm_result_t numeric_compare(struct oak_vm_t* vm,
                                            const u8 op,
                                            const struct oak_value_t a,
                                            const struct oak_value_t b)
{
  if (!(oak_is_number(a) && oak_is_number(b)))
  {
    runtime_error(vm,
                  "comparison operands must be numbers (left operand is %s, "
                  "right operand is %s)",
                  value_kind_desc(a),
                  value_kind_desc(b));
    return OAK_VM_RUNTIME_ERROR;
  }

  const float fa = coerce_f32(a);
  const float fb = coerce_f32(b);

  int result;
  switch (op)
  {
    case OAK_OP_LT:
      result = fa < fb;
      break;
    case OAK_OP_LE:
      result = fa <= fb;
      break;
    case OAK_OP_GT:
      result = fa > fb;
      break;
    case OAK_OP_GE:
      result = fa >= fb;
      break;
    default:
      runtime_error(
          vm, "internal error: unhandled comparison opcode (0x%02x)", op);
      return OAK_VM_RUNTIME_ERROR;
  }

  vm_push(vm, OAK_VALUE_BOOL(result));
  return OAK_VM_OK;
}

static enum oak_vm_result_t vm_op_call(struct oak_vm_t* vm)
{
  const u8 argc = (u8)vm_read(vm, 1);
  const usize depth = (usize)(vm->sp - vm->stack);
  if (depth < (usize)argc + 1u)
  {
    runtime_error(vm, "stack underflow in call");
    return OAK_VM_RUNTIME_ERROR;
  }

  const usize fn_slot = depth - (usize)argc - 1u;
  const struct oak_value_t fn_val = vm->stack[fn_slot];
  struct oak_value_t* arg_base = &vm->stack[fn_slot + 1u];

  if (oak_is_native_fn(fn_val))
  {
    struct oak_obj_native_fn_t* native = oak_as_native_fn(fn_val);
    if (native->arity != (int)argc)
    {
      runtime_error(vm,
                    "function arity mismatch (expected %d, got %u)",
                    native->arity,
                    (unsigned)argc);
      return OAK_VM_RUNTIME_ERROR;
    }

    struct oak_value_t result;
    const enum oak_fn_call_result_t err =
        native->fn(vm, arg_base, (int)argc, &result);
    if (err != OAK_FN_CALL_OK)
    {
      runtime_error(vm, "native function failed");
      return OAK_VM_RUNTIME_ERROR;
    }

    oak_value_decref(fn_val);
    for (u8 i = 0; i < argc; ++i)
      oak_value_decref(arg_base[i]);

    /* Native returns a value whose refcount is the single stack ownership (same
     * as after vm_push for fresh allocations). Do not incref. */
    vm->stack[fn_slot] = result;
    vm->sp = vm->stack + fn_slot + 1u;
    return OAK_VM_OK;
  }

  if (!oak_is_fn(fn_val))
  {
    runtime_error(vm, "call target is not a function");
    return OAK_VM_RUNTIME_ERROR;
  }

  struct oak_obj_fn_t* fn = oak_as_fn(fn_val);
  if (fn->arity != (int)argc)
  {
    runtime_error(vm,
                  "function arity mismatch (expected %d, got %u)",
                  fn->arity,
                  (unsigned)argc);
    return OAK_VM_RUNTIME_ERROR;
  }

  if (vm->frame_count >= OAK_FRAMES_MAX)
  {
    runtime_error(vm, "call stack overflow (max %d frames)", OAK_FRAMES_MAX);
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

static enum oak_vm_result_t vm_op_return(struct oak_vm_t* vm)
{
  if (vm->frame_count <= 0)
  {
    runtime_error(vm, "'return' outside of a function");
    return OAK_VM_RUNTIME_ERROR;
  }

  const usize depth_before = (usize)(vm->sp - vm->stack);
  if (depth_before == 0)
  {
    runtime_error(vm, "stack underflow in return");
    return OAK_VM_RUNTIME_ERROR;
  }

  struct oak_value_t result = vm_pop(vm);
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

enum oak_vm_result_t oak_vm_run(struct oak_vm_t* vm, struct oak_chunk_t* chunk)
{
  vm->chunk = chunk;
  vm->ip = chunk->bytecode;

  for (;;)
  {
    const u8 instruction = vm_read(vm, 1);
    switch (instruction)
    {
      case OAK_OP_HALT:
        return OAK_VM_OK;
      case OAK_OP_CONSTANT:
      {
        const u8 idx = vm_read(vm, 1);
        vm_push(vm, chunk->constants[idx]);
        break;
      }
      case OAK_OP_TRUE:
        vm_push(vm, OAK_VALUE_BOOL(1));
        break;
      case OAK_OP_FALSE:
        vm_push(vm, OAK_VALUE_BOOL(0));
        break;
      case OAK_OP_POP:
      {
        const struct oak_value_t val = vm_pop(vm);
        oak_value_decref(val);
        break;
      }
      case OAK_OP_GET_LOCAL:
      {
        const u8 slot = vm_read(vm, 1);
        const usize idx = vm->stack_base + (usize)slot;
        if (idx >= OAK_STACK_MAX)
        {
          runtime_error(vm, "local slot out of range (slot %u)", slot);
          return OAK_VM_RUNTIME_ERROR;
        }
        vm_push(vm, vm->stack[idx]);
        break;
      }
      case OAK_OP_SET_LOCAL:
      {
        const u8 slot = vm_read(vm, 1);
        const usize idx = vm->stack_base + (usize)slot;
        if (idx >= OAK_STACK_MAX)
        {
          runtime_error(vm, "local slot out of range (slot %u)", slot);
          return OAK_VM_RUNTIME_ERROR;
        }
        const struct oak_value_t old_val = vm->stack[idx];
        const struct oak_value_t new_val = vm_peek(vm, 0);
        oak_value_incref(new_val);
        vm->stack[idx] = new_val;
        oak_value_decref(old_val);
        break;
      }
      case OAK_OP_INC_LOCAL:
      case OAK_OP_DEC_LOCAL:
      {
        const u8 slot = vm_read(vm, 1);
        const usize idx = vm->stack_base + (usize)slot;
        if (idx >= OAK_STACK_MAX)
        {
          runtime_error(vm, "local slot out of range (slot %u)", slot);
          return OAK_VM_RUNTIME_ERROR;
        }
        const struct oak_value_t val = vm->stack[idx];
        if (!oak_is_number(val))
        {
          runtime_error(vm,
                        "local increment/decrement expects a number, got %s",
                        value_kind_desc(val));
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
      case OAK_OP_ADD:
      case OAK_OP_SUB:
      case OAK_OP_MUL:
      case OAK_OP_DIV:
      case OAK_OP_MOD:
      {
        const struct oak_value_t b = vm_pop(vm);
        const struct oak_value_t a = vm_pop(vm);

        if (instruction == OAK_OP_ADD && oak_is_string(a) && oak_is_string(b))
        {
          struct oak_obj_string_t* result =
              oak_string_concat(oak_as_string(a), oak_as_string(b));
          vm_push(vm, OAK_VALUE_OBJ(result));
          oak_obj_decref(&result->obj);
          oak_value_decref(a);
          oak_value_decref(b);
          break;
        }

        const enum oak_vm_result_t r = numeric_binary(vm, instruction, a, b);
        oak_value_decref(a);
        oak_value_decref(b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_NEGATE:
      {
        struct oak_value_t val = vm_pop(vm);
        if (!oak_is_number(val))
        {
          oak_value_decref(val);
          runtime_error(
              vm, "unary '-' expects a number, got %s", value_kind_desc(val));
          return OAK_VM_RUNTIME_ERROR;
        }
        if (oak_is_i32(val))
          vm_push(vm, OAK_VALUE_I32(-oak_as_i32(val)));
        else
          vm_push(vm, OAK_VALUE_F32(-oak_as_f32(val)));
        oak_value_decref(val);
        break;
      }
      case OAK_OP_NOT:
      {
        struct oak_value_t val = vm_pop(vm);
        vm_push(vm, OAK_VALUE_BOOL(!oak_is_truthy(val)));
        oak_value_decref(val);
        break;
      }
      case OAK_OP_EQ:
      case OAK_OP_NEQ:
      {
        struct oak_value_t b = vm_pop(vm);
        struct oak_value_t a = vm_pop(vm);
        int eq = oak_value_equal(a, b);
        vm_push(vm, OAK_VALUE_BOOL(instruction == OAK_OP_EQ ? eq : !eq));
        oak_value_decref(a);
        oak_value_decref(b);
        break;
      }
      case OAK_OP_LT:
      case OAK_OP_LE:
      case OAK_OP_GT:
      case OAK_OP_GE:
      {
        struct oak_value_t b = vm_pop(vm);
        struct oak_value_t a = vm_pop(vm);
        enum oak_vm_result_t r = numeric_compare(vm, instruction, a, b);
        oak_value_decref(a);
        oak_value_decref(b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_JUMP:
      {
        const u16 offset = vm_read(vm, 2);
        vm->ip += offset;
        break;
      }
      case OAK_OP_JUMP_IF_FALSE:
      {
        const u16 offset = vm_read(vm, 2);
        struct oak_value_t cond = vm_pop(vm);
        if (!oak_is_truthy(cond))
          vm->ip += offset;
        oak_value_decref(cond);
        break;
      }
      case OAK_OP_LOOP:
      {
        const u16 offset = vm_read(vm, 2);
        vm->ip -= offset;
        break;
      }
      case OAK_OP_CALL:
      {
        const enum oak_vm_result_t r = vm_op_call(vm);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_RETURN:
      {
        const enum oak_vm_result_t r = vm_op_return(vm);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      default:
        runtime_error(vm, "internal error: unknown opcode 0x%02x", instruction);
        return OAK_VM_RUNTIME_ERROR;
    }
  }
}
