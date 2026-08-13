#include "oak_stdlib_math.h"

#include "oak_value_impl.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

static int s_rand_seeded;

static float number_as_f32(const oak_value_t value)
{
  return oak_is_f32(value) ? oak_as_f32(value) : (float)oak_as_i32(value);
}

oak_fn_call_result_t oak_math_sqrt(oak_native_call_t* call,
                                        const oak_value_t* args,
                                        int argc,
                                        oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const float value = number_as_f32(args[0]);
  if (value < 0.0f)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(sqrtf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_sin(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(sinf(number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_cos(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(cosf(number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_tan(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(tanf(number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_abs(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (oak_is_i32(args[0]))
  {
    int v = oak_as_i32(args[0]);
    *out = OAK_VALUE_I32(v < 0 ? -v : v);
  }
  else
    *out = OAK_VALUE_F32(fabsf(oak_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_fmod(oak_native_call_t* call,
                                        const oak_value_t* args,
                                        int argc,
                                        oak_value_t* out)
{
  (void)call;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(fmodf(number_as_f32(args[0]), number_as_f32(args[1])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_min(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out)
{
  (void)call;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  float a = number_as_f32(args[0]);
  float b = number_as_f32(args[1]);
  *out = OAK_VALUE_F32(a < b ? a : b);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_max(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out)
{
  (void)call;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  float a = number_as_f32(args[0]);
  float b = number_as_f32(args[1]);
  *out = OAK_VALUE_F32(a > b ? a : b);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_random(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          int argc,
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

oak_fn_call_result_t oak_math_floor(oak_native_call_t* call,
                                         const oak_value_t* args,
                                         int argc,
                                         oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (oak_is_i32(args[0]))
    *out = args[0];
  else
    *out = OAK_VALUE_I32((int)floorf(oak_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_ceil(oak_native_call_t* call,
                                        const oak_value_t* args,
                                        int argc,
                                        oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (oak_is_i32(args[0]))
    *out = args[0];
  else
    *out = OAK_VALUE_I32((int)ceilf(oak_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_round(oak_native_call_t* call,
                                         const oak_value_t* args,
                                         int argc,
                                         oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (oak_is_i32(args[0]))
    *out = args[0];
  else
    *out = OAK_VALUE_I32((int)roundf(oak_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_pow(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out)
{
  (void)call;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(powf(number_as_f32(args[0]), number_as_f32(args[1])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_log(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const float value = number_as_f32(args[0]);
  if (value <= 0.0f)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(logf(value));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_exp(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(expf(number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_atan2(oak_native_call_t* call,
                                         const oak_value_t* args,
                                         int argc,
                                         oak_value_t* out)
{
  (void)call;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(atan2f(number_as_f32(args[0]), number_as_f32(args[1])));
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_math_sign(oak_native_call_t* call,
                                        const oak_value_t* args,
                                        int argc,
                                        oak_value_t* out)
{
  (void)call;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const float value = number_as_f32(args[0]);
  *out = OAK_VALUE_I32(value > 0.0f ? 1 : (value < 0.0f ? -1 : 0));
  return OAK_FN_CALL_OK;
}
