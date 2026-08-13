#include "oak_stdlib_math.h"

#include "oak_bind.h"
#include "oak_value_impl.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

static int s_rand_seeded;

/* Reads one numeric argument, or reports which one was wrong and why. The VM
 * checks arity before dispatching a native, so there is nothing to say here
 * about the argument count -- only about the types. */
static int arg1(oak_native_call_t* call,
                const oak_value_t* args,
                const usize argc,
                float* out)
{
  return oak_arg_number(call, args, argc, 0, out);
}

static int arg2(oak_native_call_t* call,
                const oak_value_t* args,
                const usize argc,
                float* a,
                float* b)
{
  return oak_arg_number(call, args, argc, 0, a) &&
         oak_arg_number(call, args, argc, 1, b);
}

oak_fn_call_result_t oak_math_sqrt(oak_native_call_t* call,
                                   const oak_value_t* args,
                                   const usize argc,
                                   oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (value < 0.0f)
    return oak_native_error(call, "square root of negative %g", (double)value);
  *out = OAK_VALUE_F32(sqrtf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_sin(oak_native_call_t* call,
                                  const oak_value_t* args,
                                  const usize argc,
                                  oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(sinf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_cos(oak_native_call_t* call,
                                  const oak_value_t* args,
                                  const usize argc,
                                  oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(cosf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_tan(oak_native_call_t* call,
                                  const oak_value_t* args,
                                  const usize argc,
                                  oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(tanf(value));
  return OAK_FN_CALL_OK;
}

/* Preserves the argument's representation: the absolute value of an integer is
 * an integer, so abs(-3) stays 3 rather than becoming 3.0. */
oak_fn_call_result_t oak_math_abs(oak_native_call_t* call,
                                  const oak_value_t* args,
                                  const usize argc,
                                  oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (oak_is_i32(args[0]))
  {
    const int v = oak_as_i32(args[0]);
    *out = OAK_VALUE_I32(v < 0 ? -v : v);
  }
  else
    *out = OAK_VALUE_F32(fabsf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_fmod(oak_native_call_t* call,
                                   const oak_value_t* args,
                                   const usize argc,
                                   oak_value_t* out)
{
  float a;
  float b;
  if (!arg2(call, args, argc, &a, &b))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(fmodf(a, b));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_min(oak_native_call_t* call,
                                  const oak_value_t* args,
                                  const usize argc,
                                  oak_value_t* out)
{
  float a;
  float b;
  if (!arg2(call, args, argc, &a, &b))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(a < b ? a : b);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_max(oak_native_call_t* call,
                                  const oak_value_t* args,
                                  const usize argc,
                                  oak_value_t* out)
{
  float a;
  float b;
  if (!arg2(call, args, argc, &a, &b))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(a > b ? a : b);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_random(oak_native_call_t* call,
                                     const oak_value_t* args,
                                     const usize argc,
                                     oak_value_t* out)
{
  (void)call;
  (void)args;
  (void)argc;
  if (!s_rand_seeded)
  {
    srand((unsigned)time(null));
    s_rand_seeded = 1;
  }
  *out = OAK_VALUE_F32((float)rand() / (float)RAND_MAX);
  return OAK_FN_CALL_OK;
}

/* floor/ceil/round answer with an integer, so an integer argument is already
 * the answer and passes straight through. */
oak_fn_call_result_t oak_math_floor(oak_native_call_t* call,
                                    const oak_value_t* args,
                                    const usize argc,
                                    oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = oak_is_i32(args[0]) ? args[0] : OAK_VALUE_I32((int)floorf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_ceil(oak_native_call_t* call,
                                   const oak_value_t* args,
                                   const usize argc,
                                   oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = oak_is_i32(args[0]) ? args[0] : OAK_VALUE_I32((int)ceilf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_round(oak_native_call_t* call,
                                    const oak_value_t* args,
                                    const usize argc,
                                    oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = oak_is_i32(args[0]) ? args[0] : OAK_VALUE_I32((int)roundf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_pow(oak_native_call_t* call,
                                  const oak_value_t* args,
                                  const usize argc,
                                  oak_value_t* out)
{
  float base;
  float exponent;
  if (!arg2(call, args, argc, &base, &exponent))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(powf(base, exponent));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_log(oak_native_call_t* call,
                                  const oak_value_t* args,
                                  const usize argc,
                                  oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (value <= 0.0f)
    return oak_native_error(
        call, "logarithm of non-positive %g", (double)value);
  *out = OAK_VALUE_F32(logf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_exp(oak_native_call_t* call,
                                  const oak_value_t* args,
                                  const usize argc,
                                  oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(expf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_atan2(oak_native_call_t* call,
                                    const oak_value_t* args,
                                    const usize argc,
                                    oak_value_t* out)
{
  float y;
  float x;
  if (!arg2(call, args, argc, &y, &x))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(atan2f(y, x));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_sign(oak_native_call_t* call,
                                   const oak_value_t* args,
                                   const usize argc,
                                   oak_value_t* out)
{
  float value;
  if (!arg1(call, args, argc, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_I32(value > 0.0f ? 1 : (value < 0.0f ? -1 : 0));
  return OAK_FN_CALL_OK;
}
