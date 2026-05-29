#include "oak_stdlib_math.h"

#include "oak_bind.h"
#include "oak_value.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

static int s_rand_seeded;

static float number_as_f32(const struct oak_value_t value)
{
  return oak_is_f32(value) ? oak_as_f32(value) : (float)oak_as_i32(value);
}

enum oak_fn_call_result_t oak_math_sqrt(struct oak_native_ctx_t* ctx,
                                        const struct oak_value_t* args,
                                        int argc,
                                        struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const float value = number_as_f32(args[0]);
  if (value < 0.0f)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(sqrtf(value));
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_math_sin(struct oak_native_ctx_t* ctx,
                                       const struct oak_value_t* args,
                                       int argc,
                                       struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(sinf(number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_math_cos(struct oak_native_ctx_t* ctx,
                                       const struct oak_value_t* args,
                                       int argc,
                                       struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(cosf(number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_math_tan(struct oak_native_ctx_t* ctx,
                                       const struct oak_value_t* args,
                                       int argc,
                                       struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(tanf(number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_math_abs(struct oak_native_ctx_t* ctx,
                                       const struct oak_value_t* args,
                                       int argc,
                                       struct oak_value_t* out)
{
  (void)ctx;
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

enum oak_fn_call_result_t oak_math_fmod(struct oak_native_ctx_t* ctx,
                                        const struct oak_value_t* args,
                                        int argc,
                                        struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out = OAK_VALUE_F32(fmodf(number_as_f32(args[0]), number_as_f32(args[1])));
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_math_min(struct oak_native_ctx_t* ctx,
                                       const struct oak_value_t* args,
                                       int argc,
                                       struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  float a = number_as_f32(args[0]);
  float b = number_as_f32(args[1]);
  *out = OAK_VALUE_F32(a < b ? a : b);
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_math_max(struct oak_native_ctx_t* ctx,
                                       const struct oak_value_t* args,
                                       int argc,
                                       struct oak_value_t* out)
{
  (void)ctx;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  float a = number_as_f32(args[0]);
  float b = number_as_f32(args[1]);
  *out = OAK_VALUE_F32(a > b ? a : b);
  return OAK_FN_CALL_OK;
}

enum oak_fn_call_result_t oak_math_random(struct oak_native_ctx_t* ctx,
                                          const struct oak_value_t* args,
                                          int argc,
                                          struct oak_value_t* out)
{
  (void)ctx;
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

static void bind_math_module_fn(struct oak_compile_options_t* opts,
                                const char* name,
                                oak_native_fn_t impl,
                                int arity)
{
  oak_bind_fn_global(opts,
                     &(struct oak_bind_global_fn_t){
                         .module_name = "math",
                         .name = name,
                         .impl = impl,
                         .arity = arity,
                         .return_type = OAK_BIND_SCALAR(OAK_TYPE_NUMBER),
                     });
}

void oak_stdlib_register_math(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  bind_math_module_fn(opts, "sqrt", oak_math_sqrt, 1);
  bind_math_module_fn(opts, "sin", oak_math_sin, 1);
  bind_math_module_fn(opts, "cos", oak_math_cos, 1);
  bind_math_module_fn(opts, "tan", oak_math_tan, 1);
  bind_math_module_fn(opts, "abs", oak_math_abs, 1);
  bind_math_module_fn(opts, "fmod", oak_math_fmod, 2);
  bind_math_module_fn(opts, "min", oak_math_min, 2);
  bind_math_module_fn(opts, "max", oak_math_max, 2);
  bind_math_module_fn(opts, "random", oak_math_random, 0);
}
