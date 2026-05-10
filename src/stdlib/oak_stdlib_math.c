#include "oak_stdlib_math.h"

#include "oak_bind.h"
#include "oak_value.h"

#include <math.h>

static float number_as_f32(const struct oak_value_t value)
{
  return oak_is_f32(value) ? oak_as_f32(value) : (float)oak_as_i32(value);
}

static enum oak_fn_call_result_t math_sqrt(struct oak_native_ctx_t* ctx,
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

static enum oak_fn_call_result_t math_sin(struct oak_native_ctx_t* ctx,
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

static enum oak_fn_call_result_t math_cos(struct oak_native_ctx_t* ctx,
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

static enum oak_fn_call_result_t math_tan(struct oak_native_ctx_t* ctx,
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

static void bind_math_module_fn(struct oak_compile_options_t* opts,
                                const char* name,
                                oak_native_fn_t impl,
                                int arity)
{
  oak_bind_fn(opts,
              &(struct oak_bind_fn_t){
                  .kind = OAK_BIND_FN_GLOBAL,
                  .module_name = "math",
                  .receiver_type_id = OAK_TYPE_VOID,
                  .name = name,
                  .impl = impl,
                  .arity = arity,
                  .return_type_id = OAK_TYPE_NUMBER,
                  .return_shape = OAK_BIND_SHAPE_SCALAR,
              });
}

void oak_stdlib_register_math(struct oak_compile_options_t* opts)
{
  if (!opts)
    return;
  bind_math_module_fn(opts, "sqrt", math_sqrt, 1);
  bind_math_module_fn(opts, "sin", math_sin, 1);
  bind_math_module_fn(opts, "cos", math_cos, 1);
  bind_math_module_fn(opts, "tan", math_tan, 1);
}
