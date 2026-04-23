#include "oak_compiler_internal.h"

/* Interns a freshly-allocated native function as a chunk constant and returns
 * its index. The chunk takes ownership of the single allocation reference. */
u16 oak_compiler_intern_native_constant(struct oak_compiler_t* c,
                                        const oak_native_fn_t impl,
                                        const int arity_min,
                                        const int arity_max,
                                        const char* name)
{
  struct oak_obj_native_fn_t* native =
      oak_native_fn_new(impl, arity_min, arity_max, name);
  return oak_compiler_intern_constant(c, OAK_VALUE_OBJ(&native->obj));
}

static void register_native_fn(struct oak_compiler_t* c,
                               const struct oak_native_binding_t* binding)
{
  if (c->fn_registry_count >= OAK_MAX_USER_FNS)
  {
    oak_compiler_error_at(c,
                          null,
                          "too many functions in one program (max %d)",
                          OAK_MAX_USER_FNS);
    return;
  }

  const u16 idx = oak_compiler_intern_native_constant(c,
                                                      binding->impl,
                                                      binding->arity_min,
                                                      binding->arity_max,
                                                      binding->name);

  struct oak_registered_fn_t* slot = &c->fn_registry[c->fn_registry_count++];
  slot->name = binding->name;
  slot->name_len = strlen(binding->name);
  slot->const_idx = idx;
  slot->arity_min = binding->arity_min;
  slot->arity_max = binding->arity_max;
  slot->decl = null;
}

static const struct oak_native_binding_t native_builtins[] = {
  { "print", oak_builtin_print, 1, 1 },
  { "input", oak_builtin_input, 0, 1 },
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
    oak_compiler_error_at(c, t, "map key must be of type '%s', got '%s'",
                          oak_compiler_type_full_name(c, want),
                          oak_compiler_type_full_name(c, got));
  else
    oak_compiler_error_at(c, t, "cannot push value of type '%s' to array of '%s'",
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
  { "push", oak_builtin_push, 2, OAK_TYPE_NUMBER, validate_array_push_args },
  /* len(receiver) -> length. */
  { "len", oak_builtin_len, 1, OAK_TYPE_NUMBER, null },
};

static const struct oak_builtin_method_def_t map_method_table[] = {
  /* len(receiver) -> length. */
  { "len", oak_builtin_len, 1, OAK_TYPE_NUMBER, null },
  /* has(receiver, key) -> bool. */
  { "has", oak_builtin_has, 2, OAK_TYPE_BOOL, validate_map_key_arg },
  /* delete(receiver, key) -> bool (true if removed). */
  { "delete", oak_builtin_delete, 2, OAK_TYPE_BOOL, validate_map_key_arg },
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
      oak_compiler_error_at(
          c, null, "too many %s methods (max %d)", kind, max);
      return;
    }
    const struct oak_builtin_method_def_t* def = &table[i];
    const u16 idx = oak_compiler_intern_native_constant(
        c, def->impl, def->total_arity, def->total_arity, def->name);
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
  register_method_table_from_defs(
      c, c->array_methods, &c->array_method_count, OAK_MAX_ARRAY_METHODS,
      "array", array_method_table, oak_count_of(array_method_table));
}

const struct oak_method_binding_t*
oak_compiler_find_array_method(struct oak_compiler_t* c,
                               const char* name,
                               const usize len)
{
  return method_binding_find(
      c->array_methods, c->array_method_count, name, len);
}

void oak_compiler_register_map_methods(struct oak_compiler_t* c)
{
  register_method_table_from_defs(
      c, c->map_methods, &c->map_method_count, OAK_MAX_MAP_METHODS, "map",
      map_method_table, oak_count_of(map_method_table));
}

const struct oak_method_binding_t*
oak_compiler_find_map_method(struct oak_compiler_t* c,
                             const char* name,
                             const usize len)
{
  return method_binding_find(
      c->map_methods, c->map_method_count, name, len);
}
