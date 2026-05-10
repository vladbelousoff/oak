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

static const struct oak_ast_node_t*
unwrap_call_arg(const struct oak_ast_node_t* arg_wrap)
{
  if (arg_wrap && arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
    return arg_wrap->child;
  return arg_wrap;
}

/* Reject aliasing of bindings across argument positions when at least one of
 * those positions binds to a `mut` parameter. Two `mut` aliases in the callee
 * would be exclusive-vs-exclusive aliasing; one `mut` plus one shared would
 * be exclusive-vs-shared. Either violates the borrow rules in the callee.
 * Fresh expressions (no source binding) are not tracked. */
static void check_call_arg_aliasing(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* call,
                                    const struct oak_ast_node_t* decl)
{
  enum
  {
    MAX_ARGS = 16
  };
  int sources[MAX_ARGS];
  int is_mut_param[MAX_ARGS];
  const struct oak_token_t* err_tokens[MAX_ARGS];
  int n = 0;

  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  for (int i = 0; pos != &call->children && n < MAX_ARGS;
       pos = pos->next, ++i, ++n)
  {
    const struct oak_ast_node_t* arg_wrap =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* arg_expr = unwrap_call_arg(arg_wrap);
    const struct oak_ast_node_t* param = oakc_fn_param_at(decl, i);
    sources[n] = oakc_ident_local(c, arg_expr);
    is_mut_param[n] = param ? oakc_param_is_mut(param) : 0;
    err_tokens[n] = arg_expr_error_token(arg_expr, arg_wrap);
  }

  for (int i = 0; i < n; ++i)
  {
    if (sources[i] < 0)
      continue;
    for (int j = i + 1; j < n; ++j)
    {
      if (sources[j] != sources[i])
        continue;
      if (!is_mut_param[i] && !is_mut_param[j])
        continue;
      const struct oak_local_t* l = &c->scope.locals[sources[i]];
      oak_compiler_error_at(
          c,
          err_tokens[j],
          "cannot pass '%.*s' as both argument %d and %d when at least one "
          "parameter is mutable (would alias inside the callee)",
          (int)l->length,
          l->name,
          i + 1,
          j + 1);
      return;
    }
  }
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
  if (c->has_error)
    return;
  check_call_arg_aliasing(c, call, fn->decl);
}

void oakc_check_method_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* m)
{
  if (!m->decl)
    return;
  validate_call_arg_types_for_decl(c, call, m->decl);
  if (c->has_error)
    return;
  check_call_arg_aliasing(c, call, m->decl);
}
