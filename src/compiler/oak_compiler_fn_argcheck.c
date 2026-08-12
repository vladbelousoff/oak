#include "internal/oak_compiler.h"
#include "internal/oak_interface_registry.h"

static const oak_token_t*
arg_expr_error_token(const oak_ast_node_t* arg_expr,
                     const oak_ast_node_t* arg_wrap)
{
  const oak_token_t* err_tok = arg_expr->token;
  if (!err_tok && arg_wrap->kind == OAK_NODE_FN_CALL_ARG && arg_wrap->child &&
      arg_wrap->child->token)
    err_tok = arg_wrap->child->token;
  return err_tok;
}

/* Check if `got` is acceptable for an interface-typed `want`. Returns:
 *  1 = accepted (interface coercion ok), 0 = not an interface check, -1 = rejected. */
static int check_interface_coercion(oak_compiler_t* c,
                                const oak_type_t* want,
                                const oak_type_t* got,
                                const oak_token_t* err_tok,
                                int arg_num)
{
  if (want->kind != OAK_TYPE_KIND_INTERFACE || want->is_weak)
    return 0;
  const oak_registered_interface_t* tr =
      oak_interface_find_by_id(&c->interfaces, want->id);
  if (!tr)
    return 0;
  if (oak_type_equal(want, got))
    return 1;
  const oak_registered_record_t* sd = null;
  if (got->kind == OAK_TYPE_KIND_SCALAR)
    sd = oak_records_find_by_id(&c->records, got->id);
  if (sd && oak_record_satisfies_interface(c, sd, tr))
    return 1;
  if (sd)
    oak_compiler_error_at(c, err_tok,
                          "argument %d: type '%s' does not implement interface '%s'",
                          arg_num, sd->name, tr->name);
  else
    oak_compiler_error_at(c, err_tok,
                          "argument %d: cannot coerce '%s' to interface '%s'",
                          arg_num,
                          oak_type_full_name(c, *got),
                          tr->name);
  return -1;
}

static void validate_call_arg_types_for_decl(oak_compiler_t* c,
                                             const oak_ast_node_t* call,
                                             const oak_ast_node_t* decl)
{
  if (!decl)
    return;
  const oak_list_entry_t* first = call->children.next;
  oak_list_entry_t* pos = first->next;
  for (usize i = 0; pos != &call->children; pos = pos->next, ++i)
  {
    const oak_ast_node_t* arg_wrap =
        oak_container_of(pos, oak_ast_node_t, link);
    const oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    const oak_ast_node_t* param =
        oak_fn_param_at(decl, (int)i);
    if (!param)
    {
      oak_compiler_error_at(
          c, null, "internal error: missing parameter %zu", i);
      return;
    }
    const oak_ast_node_t* want_type_node =
        oak_fn_param_type_node(param);
    if (!want_type_node)
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter (expected type name)");
      return;
    }
    oak_type_t want;
    oak_lower_type_node(c, want_type_node, &want);
    if (!oak_type_is_known(&want))
    {
      oak_compiler_error_at(
          c, param->token, "malformed function parameter type");
      return;
    }

    oak_type_t got;
    oak_infer_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      continue;

    if (oak_type_is_void(&got))
    {
      const oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, err_tok, "argument %zu cannot be void", i + 1u);
      return;
    }

    {
      const oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      const int tc = check_interface_coercion(
          c, &want, &got, err_tok, (int)(i + 1u));
      if (tc > 0)
        continue;
      if (tc < 0)
        return;
    }

    if (!oak_type_accepts(&want, &got))
    {
      const oak_token_t* err_tok =
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
      const oak_token_t* err_tok =
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
    oak_compiler_t* c,
    const oak_ast_node_t* call,
    const oak_registered_fn_t* fn)
{
  const int self_offset =
      (fn->receiver_type_id != OAK_TYPE_VOID && !fn->is_static) ? 1 : 0;
  const oak_list_entry_t* first = call->children.next;
  oak_list_entry_t* pos = first->next;
  for (int i = 0; pos != &call->children; pos = pos->next, ++i)
  {
    const int slot = i + self_offset;
    if (slot >= fn->arity)
      break;
    const oak_ast_node_t* arg_wrap =
        oak_container_of(pos, oak_ast_node_t, link);
    const oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    if (!oak_type_is_known(&fn->param_types[slot]))
      continue;
    const oak_type_t want = fn->param_types[slot];

    oak_type_t got;
    oak_infer_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      continue;
    if (oak_type_is_void(&got))
    {
      const oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, err_tok, "argument %d cannot be void", i + 1);
      return;
    }
    {
      const oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      const int tc = check_interface_coercion(c, &want, &got, err_tok, i + 1);
      if (tc > 0)
        continue;
      if (tc < 0)
        return;
    }
    if (!oak_type_accepts(&want, &got))
    {
      const oak_token_t* err_tok =
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
      const oak_token_t* err_tok =
          arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, err_tok,
                            "argument %d: cannot pass an immutable value to a "
                            "mutable parameter",
                            i + 1);
    }
  }
}

void oak_check_fn_args(
    oak_compiler_t* c,
    const oak_ast_node_t* call,
    const oak_registered_fn_t* fn)
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
    oak_compiler_t* c,
    const oak_ast_node_t* call,
    const oak_registered_fn_t* m)
{
  if (m->decl)
  {
    validate_call_arg_types_for_decl(c, call, m->decl);
    return;
  }
  if (m->param_types)
    validate_call_arg_types_for_imported(c, call, m);
}

void oak_check_args_against_decl(oak_compiler_t* c,
                                   const oak_ast_node_t* call,
                                   const oak_ast_node_t* decl)
{
  if (!decl)
    return;
  validate_call_arg_types_for_decl(c, call, decl);
}

void oak_check_interface_method_args(
    oak_compiler_t* c,
    const oak_ast_node_t* call,
    const oak_interface_method_t* tm)
{
  if (tm->sig_decl)
  {
    validate_call_arg_types_for_decl(c, call, tm->sig_decl);
    return;
  }
  if (!tm->param_types)
    return;
  oak_registered_fn_t tmp = { 0 };
  tmp.arity = tm->arity;
  tmp.param_types = tm->param_types;
  tmp.param_mut_flags = null;
  tmp.receiver_type_id = 1;
  tmp.is_static = 0;
  validate_call_arg_types_for_imported(c, call, &tmp);
}
