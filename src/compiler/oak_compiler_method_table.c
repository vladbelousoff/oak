#include "internal/oak_compiler.h"

/* Implemented in oak_compiler_method_builtins.c */
enum oak_fn_call_result_t builtin_size(struct oak_native_ctx_t*,
                                       const struct oak_value_t*,
                                       int,
                                       struct oak_value_t*);
enum oak_fn_call_result_t builtin_push(struct oak_native_ctx_t*,
                                       const struct oak_value_t*,
                                       int,
                                       struct oak_value_t*);
enum oak_fn_call_result_t builtin_has(struct oak_native_ctx_t*,
                                      const struct oak_value_t*,
                                      int,
                                      struct oak_value_t*);
enum oak_fn_call_result_t builtin_delete(struct oak_native_ctx_t*,
                                         const struct oak_value_t*,
                                         int,
                                         struct oak_value_t*);
enum oak_fn_call_result_t builtin_to_string(struct oak_native_ctx_t*,
                                            const struct oak_value_t*,
                                            int,
                                            struct oak_value_t*);
enum oak_fn_call_result_t builtin_string_format(struct oak_native_ctx_t*,
                                                const struct oak_value_t*,
                                                int,
                                                struct oak_value_t*);

static const struct oak_token_t*
first_arg_error_token(const struct oak_ast_node_t* expr,
                      const struct oak_token_t* fallback)
{
  return expr->token ? expr->token : fallback;
}

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
  oakc_infer_type(c, arg_expr, &got);
  if (!oak_type_is_known(&got))
    return;
  if (oak_type_equal(&want, &got))
    return;
  const struct oak_token_t* t = first_arg_error_token(arg_expr, err_tok);
  if (map_key_order)
    oak_compiler_error_at(c,
                          t,
                          "map key must be of type '%s', got '%s'",
                          oakc_type_full_name(c, want),
                          oakc_type_full_name(c, got));
  else
    oak_compiler_error_at(c,
                          t,
                          "cannot push value of type '%s' to array of '%s'",
                          oakc_type_full_name(c, got),
                          oakc_type_full_name(c, want));
}

static void validate_array_push_args(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* call,
                                     struct oak_type_t recv_ty,
                                     const struct oak_token_t* err_tok)
{
  const struct oak_ast_node_t* arg_expr =
      oak_compiler_fn_call_arg_expr_at(call, 0);
  if (!arg_expr)
    return;

  /* Trait element arrays accept any concrete type that structurally satisfies
   * the trait; coercion to a trait object is emitted at the call site. */
  const struct oak_registered_trait_t* elem_tr =
      oakc_trait_find_by_id(&c->traits, recv_ty.id);
  if (elem_tr)
  {
    struct oak_type_t got;
    oakc_infer_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      return;
    if (got.kind == OAK_TYPE_KIND_TRAIT && got.id == elem_tr->trait_id)
    {
      oakc_reject_immutable_ref_for_mutable_storage(
          c,
          arg_expr,
          got,
          first_arg_error_token(arg_expr, err_tok),
          "array element");
      return; /* already a matching trait object */
    }
    const struct oak_registered_record_t* sd = null;
    if (got.kind == OAK_TYPE_KIND_SCALAR)
      sd = oakc_records_find_by_id(&c->records, got.id);
    if (sd && oakc_record_satisfies_trait(c, sd, elem_tr))
    {
      oakc_reject_immutable_ref_for_mutable_storage(
          c,
          arg_expr,
          got,
          first_arg_error_token(arg_expr, err_tok),
          "array element");
      return;
    }
    const struct oak_token_t* t = first_arg_error_token(arg_expr, err_tok);
    if (sd)
      oak_compiler_error_at(c,
                            t,
                            "cannot push value of type '%s' to array of '%s': "
                            "type does not implement trait '%s'",
                            sd->name,
                            elem_tr->name,
                            elem_tr->name);
    else
      oak_compiler_error_at(c,
                            t,
                            "cannot push value of type '%s' to array of '%s'",
                            oakc_type_full_name(c, got),
                            elem_tr->name);
    return;
  }

  const struct oak_type_t element_ty = { .id = recv_ty.id };
  validate_inferred_type_matches(c, arg_expr, element_ty, err_tok, 0);
  if (!c->has_error)
    oakc_reject_immutable_ref_for_mutable_storage(
        c,
        arg_expr,
        element_ty,
        first_arg_error_token(arg_expr, err_tok),
        "array element");
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
  { "push", builtin_push, 2, OAK_TYPE_NUMBER, 1, validate_array_push_args },
  { "size", builtin_size, 1, OAK_TYPE_NUMBER, 0, null },
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static const struct oak_builtin_method_def_t map_method_table[] = {
  { "size", builtin_size, 1, OAK_TYPE_NUMBER, 0, null },
  { "has", builtin_has, 2, OAK_TYPE_BOOL, 0, validate_map_key_arg },
  { "delete", builtin_delete, 2, OAK_TYPE_BOOL, 1, validate_map_key_arg },
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static const struct oak_builtin_method_def_t string_method_table[] = {
  { "format", builtin_string_format, 2, OAK_TYPE_STRING, 0, null },
  { "size", builtin_size, 1, OAK_TYPE_NUMBER, 0, null },
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static const struct oak_builtin_method_def_t bool_method_table[] = {
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static const struct oak_builtin_method_def_t number_method_table[] = {
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static const struct oak_builtin_method_def_t record_builtin_method_table[] = {
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
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
    const u16 idx =
        oakc_intern_native_const(c, def->impl, def->total_arity, def->name);
    if (c->has_error)
      return;
    struct oak_method_binding_t* slot = &slots[(*out_count)++];
    slot->name = def->name;
    slot->name_len = oak_strlen(def->name);
    slot->const_idx = idx;
    slot->total_arity = def->total_arity;
    slot->return_type_id = def->return_type_id;
    slot->mutates_receiver = def->mutates_receiver;
    slot->validate_args = def->validate_args;
  }
}

static const struct oak_method_binding_t*
method_binding_find(const struct oak_method_binding_t* table,
                    int n,
                    const char* name)
{
  for (int i = 0; i < n; ++i)
  {
    const struct oak_method_binding_t* m = &table[i];
    if (strcmp(m->name, name) == 0)
      return m;
  }
  return null;
}

void oakc_register_array_methods(struct oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.array,
                                  &c->builtin_methods.array_count,
                                  OAK_MAX_ARRAY_METHODS,
                                  "array",
                                  array_method_table,
                                  oak_count_of(array_method_table));
}

const struct oak_method_binding_t* oakc_find_array_method(
    struct oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.array, c->builtin_methods.array_count, name);
}

void oakc_register_map_methods(struct oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.map,
                                  &c->builtin_methods.map_count,
                                  OAK_MAX_MAP_METHODS,
                                  "map",
                                  map_method_table,
                                  oak_count_of(map_method_table));
}

const struct oak_method_binding_t* oakc_find_map_method(
    struct oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.map, c->builtin_methods.map_count, name);
}

void oakc_register_string_methods(struct oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.string,
                                  &c->builtin_methods.string_count,
                                  OAK_MAX_STRING_METHODS,
                                  "string",
                                  string_method_table,
                                  oak_count_of(string_method_table));
}

const struct oak_method_binding_t* oakc_find_string_method(
    struct oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.string, c->builtin_methods.string_count, name);
}

void oakc_register_bool_methods(struct oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.bool_,
                                  &c->builtin_methods.bool_count,
                                  OAK_MAX_BOOL_METHODS,
                                  "bool",
                                  bool_method_table,
                                  oak_count_of(bool_method_table));
}

const struct oak_method_binding_t* oakc_find_bool_method(
    struct oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.bool_, c->builtin_methods.bool_count, name);
}

void oakc_register_number_methods(struct oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.number,
                                  &c->builtin_methods.number_count,
                                  OAK_MAX_NUMBER_METHODS,
                                  "number",
                                  number_method_table,
                                  oak_count_of(number_method_table));
}

const struct oak_method_binding_t* oakc_find_number_method(
    struct oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.number, c->builtin_methods.number_count, name);
}

void oakc_register_record_methods(struct oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.record,
                                  &c->builtin_methods.record_count,
                                  OAK_MAX_RECORD_BUILTIN_METHODS,
                                  "record",
                                  record_builtin_method_table,
                                  oak_count_of(record_builtin_method_table));
}

const struct oak_method_binding_t* oakc_find_record_builtin_method(
    struct oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.record, c->builtin_methods.record_count, name);
}
