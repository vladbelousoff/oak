#include "oak_vm_internal.h"

static inline float coerce_f32(const struct oak_value_t v)
{
  return oak_is_f32(v) ? oak_as_f32(v) : (float)oak_as_i32(v);
}

enum oak_vm_result_t oak_vm_numeric_binary(struct oak_vm_t* vm,
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
          oak_vm_runtime_error(vm, "integer division by zero");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = oak_as_i32(a) / oak_as_i32(b);
        break;
      case OAK_OP_MOD:
        if (oak_as_i32(b) == 0)
        {
          oak_vm_runtime_error(vm,
                               "integer remainder by zero (modulo by zero)");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = oak_as_i32(a) % oak_as_i32(b);
        break;
      default:
        oak_vm_runtime_error(
            vm, "internal error: unhandled integer opcode (0x%02x)", op);
        return OAK_VM_RUNTIME_ERROR;
    }
    oak_vm_push(vm, OAK_VALUE_I32(result));
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
          oak_vm_runtime_error(vm, "floating-point division by zero");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = fa / fb;
        break;
      case OAK_OP_MOD:
        oak_vm_runtime_error(vm,
                             "the '%%' operator is only defined for integers, "
                             "not floating-point operands");
        return OAK_VM_RUNTIME_ERROR;
      default:
        oak_vm_runtime_error(
            vm, "internal error: unhandled numeric opcode (0x%02x)", op);
        return OAK_VM_RUNTIME_ERROR;
    }

    oak_vm_push(vm, OAK_VALUE_F32(result));
    return OAK_VM_OK;
  }

  oak_vm_runtime_error(
      vm,
      "arithmetic operands must be numbers (left operand is %s, right "
      "operand is %s)",
      oak_vm_value_kind_desc(a),
      oak_vm_value_kind_desc(b));
  return OAK_VM_RUNTIME_ERROR;
}

enum oak_vm_result_t oak_vm_numeric_compare(struct oak_vm_t* vm,
                                            const u8 op,
                                            const struct oak_value_t a,
                                            const struct oak_value_t b)
{
  if (!(oak_is_number(a) && oak_is_number(b)))
  {
    oak_vm_runtime_error(vm,
                         "comparison operands must be numbers (left operand "
                         "is %s, right operand is %s)",
                         oak_vm_value_kind_desc(a),
                         oak_vm_value_kind_desc(b));
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
      oak_vm_runtime_error(
          vm, "internal error: unhandled comparison opcode (0x%02x)", op);
      return OAK_VM_RUNTIME_ERROR;
  }

  oak_vm_push(vm, OAK_VALUE_BOOL(result));
  return OAK_VM_OK;
}
