#include "oak_compiler_internal.h"

/* Interns a freshly-allocated native function as a chunk constant and returns
 * its index. The chunk takes ownership of the single allocation reference. */
u16 oak_compiler_intern_native_constant(struct oak_compiler_t* c,
                                        const oak_native_fn_t impl,
                                        const int arity,
                                        const char* name)
{
  struct oak_obj_native_fn_t* native =
      oak_native_fn_new(impl, arity, name);
  return oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));
}

static void register_native_fn(struct oak_compiler_t* c,
                               const struct oak_native_binding_t* binding)
{
  const u16 idx = oak_compiler_intern_native_constant(
      c, binding->impl, binding->arity, binding->name);

  struct oak_registered_fn_t entry = {
    .name = binding->name,
    .name_len = strlen(binding->name),
    .const_idx = idx,
    .arity = binding->arity,
    .decl = null,
  };
  oak_fn_registry_insert(&c->fns, &entry);
}

static enum oak_fn_call_result_t builtin_print(void* vm,
                                               const struct oak_value_t* args,
                                               int argc,
                                               struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_value_println(args[0]);
  *out_result = OAK_VALUE_I32(0);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_size(void* vm,
                                              const struct oak_value_t* args,
                                              int argc,
                                              struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (oak_is_array(args[0]))
  {
    *out_result = OAK_VALUE_I32((int)oak_as_array(args[0])->length);
    return OAK_FN_CALL_OK;
  }
  if (oak_is_map(args[0]))
  {
    *out_result = OAK_VALUE_I32((int)oak_as_map(args[0])->length);
    return OAK_FN_CALL_OK;
  }
  if (oak_is_string(args[0]))
  {
    *out_result = OAK_VALUE_I32((int)oak_as_string(args[0])->length);
    return OAK_FN_CALL_OK;
  }
  return OAK_FN_CALL_RUNTIME_ERROR;
}

static enum oak_fn_call_result_t builtin_push(void* vm,
                                              const struct oak_value_t* args,
                                              int argc,
                                              struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 2 || !oak_is_array(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_array_push(oak_as_array(args[0]), args[1]);
  *out_result = OAK_VALUE_I32((int)oak_as_array(args[0])->length);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_has(void* vm,
                                             const struct oak_value_t* args,
                                             int argc,
                                             struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 2 || !oak_is_map(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int found = oak_map_has(oak_as_map(args[0]), args[1]);
  *out_result = OAK_VALUE_BOOL(found);
  return OAK_FN_CALL_OK;
}

static enum oak_fn_call_result_t builtin_delete(void* vm,
                                                const struct oak_value_t* args,
                                                int argc,
                                                struct oak_value_t* out_result)
{
  (void)vm;
  if (argc != 2 || !oak_is_map(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int removed = oak_map_delete(oak_as_map(args[0]), args[1]);
  *out_result = OAK_VALUE_BOOL(removed);
  return OAK_FN_CALL_OK;
}

static const struct oak_native_binding_t native_builtins[] = {
  { "print", builtin_print, 1 },
};

void oak_compiler_register_native_builtins(struct oak_compiler_t* c)
{
  for (usize i = 0; i < oak_count_of(native_builtins); ++i)
  {
    register_native_fn(c, &native_builtins[i]);
    if (c->has_error)
      return;
  }
}

static const struct oak_token_t*
first_arg_error_token(const struct oak_ast_node_t* expr,
                      const struct oak_token_t* fallback)
{
  return expr->token ? expr->token : fallback;
}

/* If map_key_order: message is want first, then got; else got (value) then
 * want (element) for array push. */
static void
validate_inferred_type_matches(struct oak_compiler_t* c,
                               const struct oak_ast_node_t* arg_expr,
                               const struct oak_type_t want,
                               const struct oak_token_t* err_tok,
                               int map_key_order)
{
  if (!arg_expr)
    return;
  struct oak_type_t got;
  oak_compiler_infer_expr_static_type(c, arg_expr, &got);
  if (!oak_type_is_known(&got))
    return;
  if (oak_type_equal(&want, &got))
    return;
  const struct oak_token_t* t = first_arg_error_token(arg_expr, err_tok);
  if (map_key_order)
    oak_compiler_error_at(c,
                          t,
                          "map key must be of type '%s', got '%s'",
                          oak_compiler_type_full_name(c, want),
                          oak_compiler_type_full_name(c, got));
  else
    oak_compiler_error_at(c,
                          t,
                          "cannot push value of type '%s' to array of '%s'",
                          oak_compiler_type_full_name(c, got),
                          oak_compiler_type_full_name(c, want));
}

static void validate_array_push_args(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* call,
                                     struct oak_type_t recv_ty,
                                     const struct oak_token_t* err_tok)
{
  const struct oak_type_t element_ty = { .id = recv_ty.id };
  validate_inferred_type_matches(
      c, oak_compiler_fn_call_arg_expr_at(call, 0), element_ty, err_tok, 0);
}

static void validate_map_key_arg(struct oak_compiler_t* c,
                                 const struct oak_ast_node_t* call,
                                 const struct oak_type_t recv_ty,
                                 const struct oak_token_t* err_tok)
{
  const struct oak_type_t want_key = { .id = recv_ty.key_id };
  validate_inferred_type_matches(
      c, oak_compiler_fn_call_arg_expr_at(call, 0), want_key, err_tok, 1);
}

static const struct oak_builtin_method_def_t array_method_table[] = {
  /* push(receiver, value) -> new length. */
  { "push", builtin_push, 2, OAK_TYPE_NUMBER, validate_array_push_args },
  /* size(receiver) -> length. */
  { "size", builtin_size, 1, OAK_TYPE_NUMBER, null },
};

static const struct oak_builtin_method_def_t map_method_table[] = {
  /* size(receiver) -> length. */
  { "size", builtin_size, 1, OAK_TYPE_NUMBER, null },
  /* has(receiver, key) -> bool. */
  { "has", builtin_has, 2, OAK_TYPE_BOOL, validate_map_key_arg },
  /* delete(receiver, key) -> bool (true if removed). */
  { "delete", builtin_delete, 2, OAK_TYPE_BOOL, validate_map_key_arg },
};

static void
register_method_table_from_defs(struct oak_compiler_t* c,
                                struct oak_method_binding_t* slots,
                                int* out_count,
                                int max,
                                const char* kind,
                                const struct oak_builtin_method_def_t* table,
                                usize n)
{
  for (usize i = 0; i < n; ++i)
  {
    if (*out_count >= max)
    {
      oak_compiler_error_at(c, null, "too many %s methods (max %d)", kind, max);
      return;
    }
    const struct oak_builtin_method_def_t* def = &table[i];
    const u16 idx = oak_compiler_intern_native_constant(
        c, def->impl, def->total_arity, def->name);
    if (c->has_error)
      return;
    struct oak_method_binding_t* slot = &slots[(*out_count)++];
    slot->name = def->name;
    slot->name_len = strlen(def->name);
    slot->const_idx = idx;
    slot->total_arity = def->total_arity;
    slot->return_type_id = def->return_type_id;
    slot->validate_args = def->validate_args;
  }
}

static const struct oak_method_binding_t*
method_binding_find(const struct oak_method_binding_t* table,
                    int n,
                    const char* name,
                    usize len)
{
  for (int i = 0; i < n; ++i)
  {
    const struct oak_method_binding_t* m = &table[i];
    if (oak_name_eq(m->name, m->name_len, name, len))
      return m;
  }
  return null;
}

void oak_compiler_register_array_methods(struct oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.array,
                                  &c->builtin_methods.array_count,
                                  OAK_MAX_ARRAY_METHODS,
                                  "array",
                                  array_method_table,
                                  oak_count_of(array_method_table));
}

const struct oak_method_binding_t* oak_compiler_find_array_method(
    struct oak_compiler_t* c, const char* name, const usize len)
{
  return method_binding_find(
      c->builtin_methods.array, c->builtin_methods.array_count, name, len);
}

void oak_compiler_register_map_methods(struct oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.map,
                                  &c->builtin_methods.map_count,
                                  OAK_MAX_MAP_METHODS,
                                  "map",
                                  map_method_table,
                                  oak_count_of(map_method_table));
}

const struct oak_method_binding_t* oak_compiler_find_map_method(
    struct oak_compiler_t* c, const char* name, const usize len)
{
  return method_binding_find(
      c->builtin_methods.map, c->builtin_methods.map_count, name, len);
}
