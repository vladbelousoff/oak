#include "internal/oak_compiler.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

static int s_builtin_rand_seeded;

/* Interns a freshly-allocated native function as a chunk constant and returns
 * its index. The chunk takes ownership of the single allocation reference. */
u16 oakc_intern_native_const(struct oak_compiler_t* c,
                                        const oak_native_fn_t impl,
                                        const int arity,
                                        const char* name)
{
  struct oak_obj_native_fn_t* native = oak_native_fn_new(c->allocator, impl, arity, name);
  return oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));
}

static void register_native_fn(struct oak_compiler_t* c,
                               const struct oak_native_binding_t* binding)
{
  struct oak_obj_native_fn_t* native =
      oak_native_fn_new(c->allocator, binding->impl, binding->arity, binding->name);
  const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));

  struct oak_registered_fn_t entry = {
    .name = binding->name,
    .name_len = strlen(binding->name),
    .const_idx = idx,
    .arity = binding->arity,
    .return_type_id = binding->return_type_id,
    .decl = null,
    .attrs = null,
    .attr_count = 0,
  };
  oak_fn_registry_insert(&c->fns, &entry);
}

static enum oak_fn_call_result_t builtin_print(struct oak_native_ctx_t* ctx,
                                               const struct oak_value_t* args,
                                               int argc,
                                               struct oak_value_t* out_result)
{
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_value_println(ctx->allocator, args[0]);
  *out_result = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static int builtin_number_as_i32(const struct oak_value_t value)
{
  return oak_is_f32(value) ? (int)oak_as_f32(value) : oak_as_i32(value);
}

static float builtin_number_as_f32(const struct oak_value_t value)
{
  return oak_is_f32(value) ? oak_as_f32(value) : (float)oak_as_i32(value);
}

static enum oak_fn_call_result_t builtin_to_int(struct oak_native_ctx_t* ctx,
                                                const struct oak_value_t* args,
                                                int argc,
                                                struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_I32(builtin_number_as_i32(args[0]));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_to_float(struct oak_native_ctx_t* ctx,
                                                  const struct oak_value_t* args,
                                                  int argc,
                                                  struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_F32(builtin_number_as_f32(args[0]));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_is_int(struct oak_native_ctx_t* ctx,
                                                const struct oak_value_t* args,
                                                int argc,
                                                struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_BOOL(oak_is_i32(args[0]));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_is_float(struct oak_native_ctx_t* ctx,
                                                  const struct oak_value_t* args,
                                                  int argc,
                                                  struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_BOOL(oak_is_f32(args[0]));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_sqrt(struct oak_native_ctx_t* ctx,
                                              const struct oak_value_t* args,
                                              int argc,
                                              struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const float value = builtin_number_as_f32(args[0]);
  if (value < 0.0f)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_F32(sqrtf(value));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_sin(struct oak_native_ctx_t* ctx,
                                             const struct oak_value_t* args,
                                             int argc,
                                             struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_F32(sinf(builtin_number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_cos(struct oak_native_ctx_t* ctx,
                                             const struct oak_value_t* args,
                                             int argc,
                                             struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_F32(cosf(builtin_number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_tan(struct oak_native_ctx_t* ctx,
                                             const struct oak_value_t* args,
                                             int argc,
                                             struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_F32(tanf(builtin_number_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_abs(struct oak_native_ctx_t* ctx,
                                             const struct oak_value_t* args,
                                             int argc,
                                             struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1 || !oak_is_number(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (oak_is_i32(args[0]))
  {
    int v = oak_as_i32(args[0]);
    *out_result = OAK_VALUE_I32(v < 0 ? -v : v);
  }
  else
    *out_result = OAK_VALUE_F32(fabsf(oak_as_f32(args[0])));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_fmod(struct oak_native_ctx_t* ctx,
                                              const struct oak_value_t* args,
                                              int argc,
                                              struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_F32(
      fmodf(builtin_number_as_f32(args[0]), builtin_number_as_f32(args[1])));
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_min(struct oak_native_ctx_t* ctx,
                                             const struct oak_value_t* args,
                                             int argc,
                                             struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  float a = builtin_number_as_f32(args[0]);
  float b = builtin_number_as_f32(args[1]);
  *out_result = OAK_VALUE_F32(a < b ? a : b);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_max(struct oak_native_ctx_t* ctx,
                                             const struct oak_value_t* args,
                                             int argc,
                                             struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 2 || !oak_is_number(args[0]) || !oak_is_number(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  float a = builtin_number_as_f32(args[0]);
  float b = builtin_number_as_f32(args[1]);
  *out_result = OAK_VALUE_F32(a > b ? a : b);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_random(struct oak_native_ctx_t* ctx,
                                                const struct oak_value_t* args,
                                                int argc,
                                                struct oak_value_t* out_result)
{
  (void)ctx;
  (void)args;
  (void)argc;
  if (!s_builtin_rand_seeded)
  {
    srand((unsigned)time(NULL));
    s_builtin_rand_seeded = 1;
  }
  *out_result = OAK_VALUE_F32((float)rand() / (float)RAND_MAX);
  return OAK_FN_CALL_OK;
}

static const struct oak_native_binding_t native_builtins[] = {
  { "print", builtin_print, 1, OAK_TYPE_VOID },
  { "toInt", builtin_to_int, 1, OAK_TYPE_NUMBER },
  { "toFloat", builtin_to_float, 1, OAK_TYPE_NUMBER },
  { "isInt", builtin_is_int, 1, OAK_TYPE_BOOL },
  { "isFloat", builtin_is_float, 1, OAK_TYPE_BOOL },
  { "sqrt", builtin_sqrt, 1, OAK_TYPE_NUMBER },
  { "sin", builtin_sin, 1, OAK_TYPE_NUMBER },
  { "cos", builtin_cos, 1, OAK_TYPE_NUMBER },
  { "tan", builtin_tan, 1, OAK_TYPE_NUMBER },
  { "abs", builtin_abs, 1, OAK_TYPE_NUMBER },
  { "fmod", builtin_fmod, 2, OAK_TYPE_NUMBER },
  { "min", builtin_min, 2, OAK_TYPE_NUMBER },
  { "max", builtin_max, 2, OAK_TYPE_NUMBER },
  { "random", builtin_random, 0, OAK_TYPE_NUMBER },
};

void oakc_register_native_builtins(struct oak_compiler_t* c)
{
  for (usize i = 0; i < oak_count_of(native_builtins); ++i)
  {
    register_native_fn(c, &native_builtins[i]);
    if (c->has_error)
      return;
  }
}
