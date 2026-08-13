#include "internal/oak_compiler.h"
#include "oak_stdlib_math.h"
#include "oak_stdlib_string.h"

u16 oak_intern_native_const(oak_compiler_t* c,
                                        const oak_native_fn_t impl,
                                        const int arity,
                                        const char* name)
{
  oak_obj_native_fn_t* native =
      oak_native_fn_new(c->allocator, impl, arity, name, null);
  return oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));
}

static void register_native_fn(oak_compiler_t* c,
                               const oak_native_binding_t* binding)
{
  oak_obj_native_fn_t* native = oak_native_fn_new(
      c->allocator, binding->impl, binding->arity, binding->name, null);
  const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));

  oak_registered_fn_t entry = {
    .name = binding->name,
    .const_idx = idx,
    .arity = binding->arity,
    .return_type = { .id = binding->return_type_id },
    .decl = null,
    .attrs = null,
    .attr_count = 0,
    .source_module_id = OAK_MODULE_ID_NONE,
  };
  if (!oak_compiler_declare_symbol(c, null, entry.name,
                                   OAK_SYMBOL_FUNCTION,
                                   (int)oak_size(c->fns.entries),
                                   OAK_MODULE_ID_NONE, 0))
    return;
  oak_fn_registry_insert(&c->fns, &entry);
}

/* The VM matches argc against the binding's arity before dispatching, so none
 * of these re-check the argument count -- only the types they care about. */
static oak_fn_call_result_t builtin_print(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc,
                                          oak_value_t* out_result)
{
  (void)argc;
  oak_value_println(call->allocator, args[0]);
  *out_result = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t builtin_to_int(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc,
                                           oak_value_t* out_result)
{
  float value;
  if (!oak_arg_number(call, args, argc, 0, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_I32(oak_is_f32(args[0]) ? (int)value
                                                  : oak_as_i32(args[0]));
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t builtin_to_float(oak_native_call_t* call,
                                             const oak_value_t* args,
                                             const usize argc,
                                             oak_value_t* out_result)
{
  float value;
  if (!oak_arg_number(call, args, argc, 0, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_F32(value);
  return OAK_FN_CALL_OK;
}

/* Type predicates: any value is a valid argument, so there is nothing to
 * reject -- answering "no" is the whole point. */
static oak_fn_call_result_t builtin_is_int(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc,
                                           oak_value_t* out_result)
{
  (void)call;
  (void)argc;
  *out_result = OAK_VALUE_BOOL(oak_is_i32(args[0]));
  return OAK_FN_CALL_OK;
}

static oak_fn_call_result_t builtin_is_float(oak_native_call_t* call,
                                             const oak_value_t* args,
                                             const usize argc,
                                             oak_value_t* out_result)
{
  (void)call;
  (void)argc;
  *out_result = OAK_VALUE_BOOL(oak_is_f32(args[0]));
  return OAK_FN_CALL_OK;
}

static const oak_native_binding_t native_builtins[] = {
  { "print", builtin_print, 1, OAK_TYPE_VOID },
  { "to_int", builtin_to_int, 1, OAK_TYPE_NUMBER },
  { "to_float", builtin_to_float, 1, OAK_TYPE_NUMBER },
  { "is_int", builtin_is_int, 1, OAK_TYPE_BOOL },
  { "is_float", builtin_is_float, 1, OAK_TYPE_BOOL },
  { "sqrt", oak_math_sqrt, 1, OAK_TYPE_NUMBER },
  { "sin", oak_math_sin, 1, OAK_TYPE_NUMBER },
  { "cos", oak_math_cos, 1, OAK_TYPE_NUMBER },
  { "tan", oak_math_tan, 1, OAK_TYPE_NUMBER },
  { "abs", oak_math_abs, 1, OAK_TYPE_NUMBER },
  { "fmod", oak_math_fmod, 2, OAK_TYPE_NUMBER },
  { "min", oak_math_min, 2, OAK_TYPE_NUMBER },
  { "max", oak_math_max, 2, OAK_TYPE_NUMBER },
  { "random", oak_math_random, 0, OAK_TYPE_NUMBER },
  { "floor", oak_math_floor, 1, OAK_TYPE_NUMBER },
  { "ceil", oak_math_ceil, 1, OAK_TYPE_NUMBER },
  { "round", oak_math_round, 1, OAK_TYPE_NUMBER },
  { "pow", oak_math_pow, 2, OAK_TYPE_NUMBER },
  { "log", oak_math_log, 1, OAK_TYPE_NUMBER },
  { "exp", oak_math_exp, 1, OAK_TYPE_NUMBER },
  { "atan2", oak_math_atan2, 2, OAK_TYPE_NUMBER },
  { "sign", oak_math_sign, 1, OAK_TYPE_NUMBER },
  { "ord", oak_str_ord, 1, OAK_TYPE_NUMBER },
  { "chr", oak_str_chr, 1, OAK_TYPE_STRING },
  { "parse_number", oak_str_parse_number, 1, OAK_TYPE_NUMBER },
};

void oak_register_native_builtins(oak_compiler_t* c)
{
  for (usize i = 0; i < oak_count_of(native_builtins); ++i)
  {
    register_native_fn(c, &native_builtins[i]);
    if (c->has_error)
      return;
  }
}
