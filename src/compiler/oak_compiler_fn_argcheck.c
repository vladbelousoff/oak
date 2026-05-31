#include "internal/oak_compiler.h"
#include "internal/oak_trait_registry.h"

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

/* Check if `got` is acceptable for a trait-typed `want`. Returns:
 *  1 = accepted (trait coercion ok), 0 = not a trait check, -1 = rejected. */
static int check_trait_coercion(struct oak_compiler_t* c,
                                const struct oak_type_t* want,
                                const struct oak_type_t* got,
                                const struct oak_token_t* err_tok,
                                int arg_num)
{
  if (want->kind != OAK_TYPE_KIND_TRAIT || want->is_weak)
    return 0;
  const struct oak_registered_trait_t* tr =
      oak_trait_find_by_id(&c->traits, want->id);
  if (!tr)
    return 0;
  if (oak_type_equal(want, got))
    return 1;
  const struct oak_registered_record_t* sd = null;
  if (got->kind == OAK_TYPE_KIND_SCALAR)
    sd = oak_records_find_by_id(&c->records, got->id);
  if (sd && oak_record_satisfies_trait(c, sd, tr))
    return 1;
  if (sd)
    oak_compiler_error_at(c, err_tok,
                          "argument %d: type '%s' does not implement trait '%s'",
                          arg_num, sd->name, tr->name);
  else
    oak_compiler_error_at(c, err_tok,
                          "argument %d: cannot coerce '%s' to trait '%s'",
                          arg_num,
                          oak_type_full_name(c, *got),
                          tr->name);
  return -1;
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
        oak_fn_param_at(decl, (int)i);
    if (!param)
    {
      oak_compiler_error_at(
          c, null, "internal error: missing parameter %zu", i);
      return;
    }
    const struct oak_ast_node_t* want_type_node =
        oak_fn_param_type_node(param);
    if (!want_type_node)
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter (expected type name)");
      return;
    }
    struct oak_type_t want;
    oak_lower_type_node(c, want_type_node, &want);
    if (!oak_type_is_known(&want))
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter type");
      return;
    }

    struct oak_type_t got;
    oak_infer_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      continue;

    if (oak_type_is_void(&got))
    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, err_tok, "argument %zu cannot be void", i + 1u);
      return;
    }

    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      const int tc = check_trait_coercion(
          c, &want, &got, err_tok, (int)(i + 1u));
      if (tc > 0)
        continue;
      if (tc < 0)
        return;
    }

    if (!oak_type_accepts(&want, &got))
    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c,
                            err_tok,
                            "argument %zu: expected type '%s', found '%s'",
                            i + 1u,
                            oak_type_full_name(c, want),
                            oak_type_full_name(c, got));
    }

    if (oak_param_is_mut(param) &&
        oak_compiler_type_is_refcounted(c, &want) &&
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

static void validate_call_arg_types_for_imported(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* fn)
{
  const int self_offset =
      (fn->receiver_type_id != OAK_TYPE_VOID && !fn->is_static) ? 1 : 0;
  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  for (int i = 0; pos != &call->children; pos = pos->next, ++i)
  {
    const int slot = i + self_offset;
    if (slot >= fn->arity)
      break;
    const struct oak_ast_node_t* arg_wrap =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    if (!oak_type_is_known(&fn->param_types[slot]))
      continue;
    const struct oak_type_t want = fn->param_types[slot];

    struct oak_type_t got;
    oak_infer_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      continue;
    if (oak_type_is_void(&got))
    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, err_tok, "argument %d cannot be void", i + 1);
      return;
    }
    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      const int tc = check_trait_coercion(c, &want, &got, err_tok, i + 1);
      if (tc > 0)
        continue;
      if (tc < 0)
        return;
    }
    if (!oak_type_accepts(&want, &got))
    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, err_tok,
                            "argument %d: expected type '%s', found '%s'",
                            i + 1,
                            oak_type_full_name(c, want),
                            oak_type_full_name(c, got));
    }
    if (fn->param_mut_flags && fn->param_mut_flags[slot] &&
        oak_compiler_type_is_refcounted(c, &want) &&
        !oak_compiler_expr_is_mutable_place(c, arg_expr))
    {
      const struct oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, err_tok,
                            "argument %d: cannot pass an immutable value to a "
                            "mutable parameter",
                            i + 1);
    }
  }
}

void oak_check_fn_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* fn)
{
  if (fn->decl)
  {
    validate_call_arg_types_for_decl(c, call, fn->decl);
    return;
  }
  if (fn->param_types)
    validate_call_arg_types_for_imported(c, call, fn);
}

void oak_check_method_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* m)
{
  if (m->decl)
  {
    validate_call_arg_types_for_decl(c, call, m->decl);
    return;
  }
  if (m->param_types)
    validate_call_arg_types_for_imported(c, call, m);
}

void oak_check_args_against_decl(struct oak_compiler_t* c,
                                   const struct oak_ast_node_t* call,
                                   const struct oak_ast_node_t* decl)
{
  if (!decl)
    return;
  validate_call_arg_types_for_decl(c, call, decl);
}

void oak_check_trait_method_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_trait_method_t* tm)
{
  if (tm->sig_decl)
  {
    validate_call_arg_types_for_decl(c, call, tm->sig_decl);
    return;
  }
  if (!tm->param_types)
    return;
  struct oak_registered_fn_t tmp = { 0 };
  tmp.arity = tm->arity;
  tmp.param_types = tm->param_types;
  tmp.param_mut_flags = null;
  tmp.receiver_type_id = 1;
  tmp.is_static = 0;
  validate_call_arg_types_for_imported(c, call, &tmp);
}
