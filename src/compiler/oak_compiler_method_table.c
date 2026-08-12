#include "internal/oak_compiler.h"
#include "oak_stdlib_string.h"

/* Implemented in oak_compiler_method_builtins.c */
oak_fn_call_result_t builtin_size(oak_native_ctx_t*,
                                       const oak_value_t*,
                                       int,
                                       oak_value_t*);
oak_fn_call_result_t builtin_push(oak_native_ctx_t*,
                                       const oak_value_t*,
                                       int,
                                       oak_value_t*);
oak_fn_call_result_t builtin_has(oak_native_ctx_t*,
                                      const oak_value_t*,
                                      int,
                                      oak_value_t*);
oak_fn_call_result_t builtin_delete(oak_native_ctx_t*,
                                         const oak_value_t*,
                                         int,
                                         oak_value_t*);
oak_fn_call_result_t builtin_to_string(oak_native_ctx_t*,
                                            const oak_value_t*,
                                            int,
                                            oak_value_t*);
oak_fn_call_result_t builtin_string_format(oak_native_ctx_t*,
                                                const oak_value_t*,
                                                int,
                                                oak_value_t*);

static const oak_token_t*
first_arg_error_token(const oak_ast_node_t* expr,
                      const oak_token_t* fallback)
{
  return expr->token ? expr->token : fallback;
}

static void
validate_inferred_type_matches(oak_compiler_t* c,
                               const oak_ast_node_t* arg_expr,
                               const oak_type_t want,
                               const oak_token_t* err_tok,
                               int map_key_order)
{
  if (!arg_expr)
    return;
  oak_type_t got;
  oak_infer_type(c, arg_expr, &got);
  if (!oak_type_is_known(&got))
    return;
  if (oak_type_equal(&want, &got))
    return;
  const oak_token_t* t = first_arg_error_token(arg_expr, err_tok);
  if (map_key_order)
    oak_compiler_error_at(c,
                          t,
                          "map key must be of type '%s', got '%s'",
                          oak_type_full_name(c, want),
                          oak_type_full_name(c, got));
  else
    oak_compiler_error_at(c,
                          t,
                          "cannot push value of type '%s' to array of '%s'",
                          oak_type_full_name(c, got),
                          oak_type_full_name(c, want));
}

static void validate_array_push_args(oak_compiler_t* c,
                                     const oak_ast_node_t* call,
                                     oak_type_t recv_ty,
                                     const oak_token_t* err_tok)
{
  const oak_ast_node_t* arg_expr =
      oak_compiler_fn_call_arg_expr_at(call, 0);
  if (!arg_expr)
    return;

  if (oak_container_store_locked(c, &recv_ty))
  {
    oak_compiler_error_at(c,
                          first_arg_error_token(arg_expr, err_tok),
                          "cannot push into '%s': its element type lies on a "
                          "strong reference cycle, so the array is fixed at "
                          "construction (use weak links to break the cycle)",
                          oak_type_full_name(c, recv_ty));
    return;
  }

  /* Interface element arrays accept any concrete type that structurally satisfies
   * the interface; coercion to an interface object is emitted at the call site. */
  const oak_registered_interface_t* elem_tr =
      oak_interface_find_by_id(&c->interfaces, recv_ty.id);
  if (elem_tr)
  {
    oak_type_t got;
    oak_infer_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      return;
    if (got.kind == OAK_TYPE_KIND_INTERFACE && got.id == elem_tr->interface_id)
    {
      oak_reject_immutable_ref_for_mutable_storage(
          c,
          arg_expr,
          got,
          first_arg_error_token(arg_expr, err_tok),
          "array element");
      return; /* already a matching interface object */
    }
    const oak_registered_record_t* sd = null;
    if (got.kind == OAK_TYPE_KIND_SCALAR)
      sd = oak_records_find_by_id(&c->records, got.id);
    if (sd && oak_record_satisfies_interface(c, sd, elem_tr))
    {
      oak_reject_immutable_ref_for_mutable_storage(
          c,
          arg_expr,
          got,
          first_arg_error_token(arg_expr, err_tok),
          "array element");
      return;
    }
    const oak_token_t* t = first_arg_error_token(arg_expr, err_tok);
    if (sd)
      oak_compiler_error_at(c,
                            t,
                            "cannot push value of type '%s' to array of '%s': "
                            "type does not implement interface '%s'",
                            sd->name,
                            elem_tr->name,
                            elem_tr->name);
    else
      oak_compiler_error_at(c,
                            t,
                            "cannot push value of type '%s' to array of '%s'",
                            oak_type_full_name(c, got),
                            elem_tr->name);
    return;
  }

  const oak_type_t element_ty = { .id = recv_ty.id };
  validate_inferred_type_matches(c, arg_expr, element_ty, err_tok, 0);
  if (!c->has_error)
    oak_reject_immutable_ref_for_mutable_storage(
        c,
        arg_expr,
        element_ty,
        first_arg_error_token(arg_expr, err_tok),
        "array element");
}

static void validate_map_key_arg(oak_compiler_t* c,
                                 const oak_ast_node_t* call,
                                 const oak_type_t recv_ty,
                                 const oak_token_t* err_tok)
{
  const oak_type_t want_key = { .id = recv_ty.key_id };
  validate_inferred_type_matches(
      c, oak_compiler_fn_call_arg_expr_at(call, 0), want_key, err_tok, 1);
}

static const oak_builtin_method_def_t array_method_table[] = {
  { "push", builtin_push, 2, OAK_TYPE_NUMBER, 1, validate_array_push_args },
  { "size", builtin_size, 1, OAK_TYPE_NUMBER, 0, null },
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static const oak_builtin_method_def_t map_method_table[] = {
  { "size", builtin_size, 1, OAK_TYPE_NUMBER, 0, null },
  { "has", builtin_has, 2, OAK_TYPE_BOOL, 0, validate_map_key_arg },
  { "delete", builtin_delete, 2, OAK_TYPE_BOOL, 1, validate_map_key_arg },
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static const oak_builtin_method_def_t string_method_table[] = {
  { "format", builtin_string_format, 2, OAK_TYPE_STRING, 0, null },
  { "size", builtin_size, 1, OAK_TYPE_NUMBER, 0, null },
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
  { "upper", oak_str_upper, 1, OAK_TYPE_STRING, 0, null },
  { "lower", oak_str_lower, 1, OAK_TYPE_STRING, 0, null },
  { "trim", oak_str_trim, 1, OAK_TYPE_STRING, 0, null },
  { "contains", oak_str_contains, 2, OAK_TYPE_BOOL, 0, null },
  { "starts_with", oak_str_starts_with, 2, OAK_TYPE_BOOL, 0, null },
  { "ends_with", oak_str_ends_with, 2, OAK_TYPE_BOOL, 0, null },
  { "index_of", oak_str_index_of, 2, OAK_TYPE_NUMBER, 0, null },
  { "replace", oak_str_replace, 3, OAK_TYPE_STRING, 0, null },
  { "repeat", oak_str_repeat, 2, OAK_TYPE_STRING, 0, null },
  { "substring", oak_str_substring, 3, OAK_TYPE_STRING, 0, null },
  { "to_snake_case", oak_str_to_snake_case, 1, OAK_TYPE_STRING, 0, null },
  { "to_camel_case", oak_str_to_camel_case, 1, OAK_TYPE_STRING, 0, null },
};

static const oak_builtin_method_def_t bool_method_table[] = {
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static const oak_builtin_method_def_t number_method_table[] = {
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static const oak_builtin_method_def_t record_builtin_method_table[] = {
  { "to_string", builtin_to_string, 1, OAK_TYPE_STRING, 0, null },
};

static void
register_method_table_from_defs(oak_compiler_t* c,
                                oak_method_binding_t* slots,
                                int* out_count,
                                int max,
                                const char* kind,
                                const oak_builtin_method_def_t* table,
                                usize n)
{
  for (usize i = 0; i < n; ++i)
  {
    if (*out_count >= max)
    {
      oak_compiler_error_at(c, null, "too many %s methods (max %d)", kind, max);
      return;
    }
    const oak_builtin_method_def_t* def = &table[i];
    const u16 idx =
        oak_intern_native_const(c, def->impl, def->total_arity, def->name);
    if (c->has_error)
      return;
    oak_method_binding_t* slot = &slots[(*out_count)++];
    slot->name = def->name;
    slot->const_idx = idx;
    slot->total_arity = def->total_arity;
    slot->return_type_id = def->return_type_id;
    slot->mutates_receiver = def->mutates_receiver;
    slot->validate_args = def->validate_args;
  }
}

static const oak_method_binding_t*
method_binding_find(const oak_method_binding_t* table,
                    int n,
                    const char* name)
{
  for (int i = 0; i < n; ++i)
  {
    const oak_method_binding_t* m = &table[i];
    if (strcmp(m->name, name) == 0)
      return m;
  }
  return null;
}

void oak_register_array_methods(oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.array,
                                  &c->builtin_methods.array_count,
                                  OAK_MAX_ARRAY_METHODS,
                                  "array",
                                  array_method_table,
                                  oak_count_of(array_method_table));
}

const oak_method_binding_t* oak_find_array_method(
    oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.array, c->builtin_methods.array_count, name);
}

void oak_register_map_methods(oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.map,
                                  &c->builtin_methods.map_count,
                                  OAK_MAX_MAP_METHODS,
                                  "map",
                                  map_method_table,
                                  oak_count_of(map_method_table));
}

const oak_method_binding_t* oak_find_map_method(
    oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.map, c->builtin_methods.map_count, name);
}

void oak_register_string_methods(oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.string,
                                  &c->builtin_methods.string_count,
                                  OAK_MAX_STRING_METHODS,
                                  "string",
                                  string_method_table,
                                  oak_count_of(string_method_table));
}

const oak_method_binding_t* oak_find_string_method(
    oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.string, c->builtin_methods.string_count, name);
}

void oak_register_bool_methods(oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.bool_,
                                  &c->builtin_methods.bool_count,
                                  OAK_MAX_BOOL_METHODS,
                                  "bool",
                                  bool_method_table,
                                  oak_count_of(bool_method_table));
}

const oak_method_binding_t* oak_find_bool_method(
    oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.bool_, c->builtin_methods.bool_count, name);
}

void oak_register_number_methods(oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.number,
                                  &c->builtin_methods.number_count,
                                  OAK_MAX_NUMBER_METHODS,
                                  "number",
                                  number_method_table,
                                  oak_count_of(number_method_table));
}

const oak_method_binding_t* oak_find_number_method(
    oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.number, c->builtin_methods.number_count, name);
}

void oak_register_record_methods(oak_compiler_t* c)
{
  register_method_table_from_defs(c,
                                  c->builtin_methods.record,
                                  &c->builtin_methods.record_count,
                                  OAK_MAX_RECORD_BUILTIN_METHODS,
                                  "record",
                                  record_builtin_method_table,
                                  oak_count_of(record_builtin_method_table));
}

const oak_method_binding_t* oak_find_record_builtin_method(
    oak_compiler_t* c, const char* name)
{
  return method_binding_find(
      c->builtin_methods.record, c->builtin_methods.record_count, name);
}
