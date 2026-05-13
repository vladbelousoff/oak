#include "internal/oak_compiler.h"

static const struct oak_token_t*
arg_expr_error_token(const struct oak_ast_node_t* arg_expr,
                     const struct oak_ast_node_t* arg_wrap)
{
  const struct oak_token_t* err_tok = arg_expr->token;
  if (!err_tok && arg_wrap->kind == OAK_NODE_FN_CALL_ARG && arg_wrap->child &&
      arg_wrap->child->token)
    err_tok = arg_wrap->child->token;
  return err_tok;
}

static void validate_call_arg_types_for_decl(struct oak_compiler_t* c,
                                             const struct oak_ast_node_t* call,
                                             const struct oak_ast_node_t* decl)
{
  if (!decl)
    return;
  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  for (usize i = 0; pos != &call->children; pos = pos->next, ++i)
  {
    const struct oak_ast_node_t* arg_wrap =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    const struct oak_ast_node_t* param =
        oakc_fn_param_at(decl, (int)i);
    if (!param)
    {
      oak_compiler_error_at(
          c, null, "internal error: missing parameter %zu", i);
      return;
    }
    const struct oak_ast_node_t* want_type_node =
        oakc_fn_param_type_node(param);
    if (!want_type_node)
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter (expected type name)");
      return;
    }
    struct oak_type_t want;
    oakc_lower_type_node(c, want_type_node, &want);
    if (!oak_type_is_known(&want))
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter type");
      return;
    }

    struct oak_type_t got;
    oakc_infer_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      continue;

    if (oak_type_is_void(&got))
    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, err_tok, "argument %zu cannot be void", i + 1u);
      return;
    }

    /* Structural trait coercion: a concrete record satisfying the trait is
     * accepted without an exact type match. */
    if (want.kind == OAK_TYPE_KIND_TRAIT)
    {
      const struct oak_registered_trait_t* tr =
          oakc_trait_find_by_id(&c->traits, want.id);
      if (tr)
      {
        if (oak_type_equal(&want, &got))
          continue; /* already a trait object of the same trait */
        const struct oak_registered_record_t* sd = null;
        if (got.kind == OAK_TYPE_KIND_SCALAR)
          sd = oakc_records_find_by_id(&c->records, got.id);
        if (sd && oakc_record_satisfies_trait(c, sd, tr))
          continue; /* concrete type structurally satisfies the trait */
        const struct oak_token_t* err_tok =
            arg_expr_error_token(arg_expr, arg_wrap);
        if (sd)
          oak_compiler_error_at(c,
                                err_tok,
                                "argument %zu: type '%s' does not implement "
                                "trait '%s'",
                                i + 1u,
                                sd->name,
                                tr->name);
        else
          oak_compiler_error_at(c,
                                err_tok,
                                "argument %zu: cannot coerce '%s' to trait '%s'",
                                i + 1u,
                                oakc_type_full_name(c, got),
                                tr->name);
        return;
      }
    }

    if (!oak_type_equal(&want, &got))
    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c,
                            err_tok,
                            "argument %zu: expected type '%s', found '%s'",
                            i + 1u,
                            oakc_type_full_name(c, want),
                            oakc_type_full_name(c, got));
    }

    if (oakc_param_is_mut(param) &&
        oak_type_is_refcounted(&want) &&
        !oak_compiler_expr_is_mutable_place(c, arg_expr))
    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c,
                            err_tok,
                            "argument %zu: cannot pass an immutable value to a "
                            "mutable parameter",
                            i + 1u);
    }
  }
}

void oakc_check_fn_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* fn)
{
  if (!fn->decl)
    return;
  validate_call_arg_types_for_decl(c, call, fn->decl);
}

void oakc_check_method_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* m)
{
  if (!m->decl)
    return;
  validate_call_arg_types_for_decl(c, call, m->decl);
}

void oakc_check_args_against_decl(struct oak_compiler_t* c,
                                   const struct oak_ast_node_t* call,
                                   const struct oak_ast_node_t* decl)
{
  if (!decl)
    return;
  validate_call_arg_types_for_decl(c, call, decl);
}
