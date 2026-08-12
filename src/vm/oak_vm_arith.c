#include "internal/oak_vm.h"

#include <limits.h>

/* Convert a number to i32 for '//'. Fails on NaN and on floats outside the
 * i32 range, where a plain cast would be undefined behavior. */
static inline int checked_i32(const oak_value_t v, int* out)
{
  if (oak_is_i32(v))
  {
    *out = oak_as_i32(v);
    return 1;
  }
  const float f = oak_as_f32(v);
  if (!(f >= -2147483648.0f && f < 2147483648.0f))
    return 0;
  *out = (int)f;
  return 1;
}

static inline float coerce_f32(const oak_value_t v)
{
  return oak_is_f32(v) ? oak_as_f32(v) : (float)oak_as_i32(v);
}

oak_vm_result_t oak_vm_numeric_binary(oak_vm_t* vm,
                                           const u8 binop,
                                           const oak_value_t a,
                                           const oak_value_t b)
{
  if (binop == OAK_BINOP_INT_DIVIDE)
  {
    if (!oak_is_number(a) || !oak_is_number(b))
    {
      oak_vm_runtime_error(
          vm,
          "integer division operands must be numbers (left operand is %s, "
          "right operand is %s)",
          oak_vm_value_kind_desc(a),
          oak_vm_value_kind_desc(b));
      return OAK_VM_RUNTIME_ERROR;
    }
    int dividend;
    int divisor;
    if (!checked_i32(a, &dividend) || !checked_i32(b, &divisor))
    {
      oak_vm_runtime_error(
          vm, "integer division operand does not fit in an integer");
      return OAK_VM_RUNTIME_ERROR;
    }
    if (divisor == 0)
    {
      oak_vm_runtime_error(vm, "integer division by zero");
      return OAK_VM_RUNTIME_ERROR;
    }
    if (dividend == INT_MIN && divisor == -1)
    {
      oak_vm_runtime_error(vm, "integer division overflow");
      return OAK_VM_RUNTIME_ERROR;
    }
    return oak_vm_push(vm, OAK_VALUE_I32(dividend / divisor));
  }

  if (oak_is_i32(a) && oak_is_i32(b) && binop != OAK_BINOP_DIVIDE)
  {
    int result;
    switch (binop)
    {
      case OAK_BINOP_ADD:
        result = oak_i32_wrap_add(oak_as_i32(a), oak_as_i32(b));
        break;
      case OAK_BINOP_SUBTRACT:
        result = oak_i32_wrap_sub(oak_as_i32(a), oak_as_i32(b));
        break;
      case OAK_BINOP_MULTIPLY:
        result = oak_i32_wrap_mul(oak_as_i32(a), oak_as_i32(b));
        break;
      case OAK_BINOP_MODULO:
        if (oak_as_i32(b) == 0)
        {
          oak_vm_runtime_error(vm,
                               "integer remainder by zero (modulo by zero)");
          return OAK_VM_RUNTIME_ERROR;
        }
        /* INT_MIN % -1 is mathematically 0 but traps on x86. */
        result = oak_as_i32(b) == -1 ? 0 : oak_as_i32(a) % oak_as_i32(b);
        break;
      default:
        oak_vm_runtime_error(
            vm, "internal error: unhandled integer binop (0x%02x)", binop);
        return OAK_VM_RUNTIME_ERROR;
    }
    return oak_vm_push(vm, OAK_VALUE_I32(result));
  }

  if (oak_is_number(a) && oak_is_number(b))
  {
    const float fa = coerce_f32(a);
    const float fb = coerce_f32(b);

    float result;
    switch (binop)
    {
      case OAK_BINOP_ADD:
        result = fa + fb;
        break;
      case OAK_BINOP_SUBTRACT:
        result = fa - fb;
        break;
      case OAK_BINOP_MULTIPLY:
        result = fa * fb;
        break;
      case OAK_BINOP_DIVIDE:
        if (fb == 0.0f)
        {
          oak_vm_runtime_error(vm, "floating-point division by zero");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = fa / fb;
        break;
      case OAK_BINOP_MODULO:
        oak_vm_runtime_error(vm,
                             "the '%%' operator is only defined for integers, "
                             "not floating-point operands");
        return OAK_VM_RUNTIME_ERROR;
      default:
        oak_vm_runtime_error(
            vm, "internal error: unhandled numeric binop (0x%02x)", binop);
        return OAK_VM_RUNTIME_ERROR;
    }

    return oak_vm_push(vm, OAK_VALUE_F32(result));
  }

  oak_vm_runtime_error(
      vm,
      "arithmetic operands must be numbers (left operand is %s, right "
      "operand is %s)",
      oak_vm_value_kind_desc(a),
      oak_vm_value_kind_desc(b));
  return OAK_VM_RUNTIME_ERROR;
}

oak_vm_result_t oak_vm_numeric_compare(oak_vm_t* vm,
                                            const u8 binop,
                                            const oak_value_t a,
                                            const oak_value_t b)
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

  int result;

  if (oak_is_i32(a) && oak_is_i32(b))
  {
    const int ia = oak_as_i32(a);
    const int ib = oak_as_i32(b);
    switch (binop)
    {
      case OAK_BINOP_LESS:          result = ia < ib;  break;
      case OAK_BINOP_LESS_EQUAL:    result = ia <= ib; break;
      case OAK_BINOP_GREATER:       result = ia > ib;  break;
      case OAK_BINOP_GREATER_EQUAL: result = ia >= ib; break;
      default:
        oak_vm_runtime_error(
            vm, "internal error: unhandled comparison binop (0x%02x)", binop);
        return OAK_VM_RUNTIME_ERROR;
    }
  }
  else
  {
    const float fa = coerce_f32(a);
    const float fb = coerce_f32(b);
    switch (binop)
    {
      case OAK_BINOP_LESS:          result = fa < fb;  break;
      case OAK_BINOP_LESS_EQUAL:    result = fa <= fb; break;
      case OAK_BINOP_GREATER:       result = fa > fb;  break;
      case OAK_BINOP_GREATER_EQUAL: result = fa >= fb; break;
      default:
        oak_vm_runtime_error(
            vm, "internal error: unhandled comparison binop (0x%02x)", binop);
        return OAK_VM_RUNTIME_ERROR;
    }
  }

  return oak_vm_push(vm, OAK_VALUE_BOOL(result));
}
