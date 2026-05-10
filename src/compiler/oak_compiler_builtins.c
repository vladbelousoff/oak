#include "internal/oak_compiler.h"

#include <math.h>

/* Interns a freshly-allocated native function as a chunk constant and returns
 * its index. The chunk takes ownership of the single allocation reference. */
u16 oakc_intern_native_const(struct oak_compiler_t* c,
                                        const oak_native_fn_t impl,
                                        const int arity,
                                        const char* name)
{
  struct oak_obj_native_fn_t* native = oak_native_fn_new(impl, arity, name);
  return oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));
}

static void register_native_fn(struct oak_compiler_t* c,
                               const struct oak_native_binding_t* binding)
{
  const u16 idx = oakc_intern_native_const(
      c, binding->impl, binding->arity, binding->name);

  struct oak_registered_fn_t entry = {
    .name = binding->name,
    .name_len = strlen(binding->name),
    .const_idx = idx,
    .arity = binding->arity,
    .return_type_id = binding->return_type_id,
    .decl = null,
  };
  oak_fn_registry_insert(&c->fns, &entry);
}

static enum oak_fn_call_result_t builtin_print(struct oak_native_ctx_t* ctx,
                                               const struct oak_value_t* args,
                                               int argc,
                                               struct oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_value_println(args[0]);
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

static const struct oak_native_binding_t native_builtins[] = {
  { "print", builtin_print, 1, OAK_TYPE_VOID },
  { "to_int", builtin_to_int, 1, OAK_TYPE_NUMBER },
  { "to_float", builtin_to_float, 1, OAK_TYPE_NUMBER },
  { "is_int", builtin_is_int, 1, OAK_TYPE_BOOL },
  { "is_float", builtin_is_float, 1, OAK_TYPE_BOOL },
  { "sqrt", builtin_sqrt, 1, OAK_TYPE_NUMBER },
  { "sin", builtin_sin, 1, OAK_TYPE_NUMBER },
  { "cos", builtin_cos, 1, OAK_TYPE_NUMBER },
  { "tan", builtin_tan, 1, OAK_TYPE_NUMBER },
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
