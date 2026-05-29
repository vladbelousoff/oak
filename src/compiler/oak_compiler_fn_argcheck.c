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

/* self_offset is the number of leading param_types slots not matched against
 * call arguments — 1 for native instance methods (slot 0 is the implicit
 * self), 0 otherwise. */
static void validate_generic_call_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* fn,
    int self_offset)
{
  const struct oak_generic_def_t* def =
      &c->generics.defs[fn->generic_def_index];

  struct oak_generic_param_t* saved_gp = c->generic_params;
  int saved_gpc = c->generic_param_count;
  c->generic_params = def->params;
  c->generic_param_count = def->param_count;

  struct oak_type_t bindings[OAK_MAX_GENERIC_PARAMS];
  for (int b = 0; b < def->param_count; ++b)
    oak_type_clear(&bindings[b]);

  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  for (int i = 0; pos != &call->children; pos = pos->next, ++i)
  {
    const struct oak_ast_node_t* arg_wrap =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    const struct oak_ast_node_t* param =
        fn->decl ? oak_fn_param_at(fn->decl, i) : null;
    struct oak_type_t want;
    if (fn->decl)
    {
      if (!param)
        continue;
      const struct oak_ast_node_t* tn = oak_fn_param_type_node(param);
      if (!tn)
        continue;
      oak_lower_type_node(c, tn, &want);
    }
    else
    {
      /* Native generic fn: parameter types come from the binding, not an AST. */
      if (!fn->param_types || i + self_offset >= fn->arity)
        continue;
      want = fn->param_types[i + self_offset];
    }
    if (!oak_type_is_known(&want))
      continue;

    struct oak_type_t got;
    oak_infer_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      continue;

    if (oak_type_is_void(&got))
    {
      const struct oak_token_t* et = arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, et, "argument %d cannot be void", i + 1);
      goto done;
    }

    int pi = -1;
    if (want.kind == OAK_TYPE_KIND_PARAM)
      pi = (int)(want.id - OAK_TYPE_PARAM_BASE);

    if (pi >= 0 && pi < def->param_count)
    {
      if (!oak_type_is_known(&bindings[pi]))
      {
        bindings[pi] = got;
      }
      else if (!oak_type_equal(&bindings[pi], &got))
      {
        const struct oak_token_t* et = arg_expr_error_token(arg_expr, arg_wrap);
        oak_compiler_error_at(
            c, et,
            "argument %d: type '%s' conflicts with earlier use of "
            "type parameter '%s' as '%s'",
            i + 1,
            oak_type_full_name(c, got),
            def->params[pi].name,
            oak_type_full_name(c, bindings[pi]));
        goto done;
      }
      continue;
    }

    if (want.kind == OAK_TYPE_KIND_ARRAY && got.kind == OAK_TYPE_KIND_ARRAY &&
        want.id >= OAK_TYPE_PARAM_BASE)
    {
      pi = (int)(want.id - OAK_TYPE_PARAM_BASE);
      if (pi >= 0 && pi < def->param_count)
      {
        struct oak_type_t elem;
        oak_type_clear(&elem);
        elem.id = got.id;
        if (!oak_type_is_known(&bindings[pi]))
          bindings[pi] = elem;
        else if (!oak_type_equal(&bindings[pi], &elem))
        {
          const struct oak_token_t* et = arg_expr_error_token(arg_expr, arg_wrap);
          oak_compiler_error_at(
              c, et,
              "argument %d: element type conflicts with earlier use of "
              "type parameter '%s'",
              i + 1, def->params[pi].name);
          goto done;
        }
        continue;
      }
    }

    /* Map parameters carry independent value (want.id) and key (want.key_id)
     * type params; bind and conflict-check each separately. */
    if (want.kind == OAK_TYPE_KIND_MAP && got.kind == OAK_TYPE_KIND_MAP &&
        (want.id >= OAK_TYPE_PARAM_BASE || want.key_id >= OAK_TYPE_PARAM_BASE))
    {
      const struct oak_token_t* et = arg_expr_error_token(arg_expr, arg_wrap);
      /* Concrete (non-parameter) halves of the map must still match exactly. */
      if (want.id < OAK_TYPE_PARAM_BASE && got.id != want.id)
      {
        oak_compiler_error_at(c, et,
                              "argument %d: expected type '%s', found '%s'",
                              i + 1, oak_type_full_name(c, want),
                              oak_type_full_name(c, got));
        goto done;
      }
      if (want.key_id < OAK_TYPE_PARAM_BASE && got.key_id != want.key_id)
      {
        oak_compiler_error_at(c, et,
                              "argument %d: expected type '%s', found '%s'",
                              i + 1, oak_type_full_name(c, want),
                              oak_type_full_name(c, got));
        goto done;
      }
      if (want.id >= OAK_TYPE_PARAM_BASE)
      {
        const int vpi = (int)(want.id - OAK_TYPE_PARAM_BASE);
        if (vpi >= 0 && vpi < def->param_count)
        {
          struct oak_type_t v;
          oak_type_clear(&v);
          v.id = got.id;
          if (!oak_type_is_known(&bindings[vpi]))
            bindings[vpi] = v;
          else if (!oak_type_equal(&bindings[vpi], &v))
          {
            oak_compiler_error_at(
                c, et,
                "argument %d: map value type conflicts with earlier use of "
                "type parameter '%s'",
                i + 1, def->params[vpi].name);
            goto done;
          }
        }
      }
      if (want.key_id >= OAK_TYPE_PARAM_BASE)
      {
        const int kpi = (int)(want.key_id - OAK_TYPE_PARAM_BASE);
        if (kpi >= 0 && kpi < def->param_count)
        {
          struct oak_type_t k;
          oak_type_clear(&k);
          k.id = got.key_id;
          if (!oak_type_is_known(&bindings[kpi]))
            bindings[kpi] = k;
          else if (!oak_type_equal(&bindings[kpi], &k))
          {
            oak_compiler_error_at(
                c, et,
                "argument %d: map key type conflicts with earlier use of "
                "type parameter '%s'",
                i + 1, def->params[kpi].name);
            goto done;
          }
        }
      }
      continue;
    }

    {
      const struct oak_token_t* et = arg_expr_error_token(arg_expr, arg_wrap);
      const int tc = check_trait_coercion(c, &want, &got, et, i + 1);
      if (tc > 0)
        continue;
      if (tc < 0)
        goto done;
    }

    if (!oak_type_accepts(&want, &got))
    {
      const struct oak_token_t* et = arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, et,
                            "argument %d: expected type '%s', found '%s'",
                            i + 1,
                            oak_type_full_name(c, want),
                            oak_type_full_name(c, got));
    }

    if (param && oak_param_is_mut(param) &&
        oak_compiler_type_is_refcounted(c, &want) &&
        !oak_compiler_expr_is_mutable_place(c, arg_expr))
    {
      const struct oak_token_t* et = arg_expr_error_token(arg_expr, arg_wrap);
      oak_compiler_error_at(c, et,
                            "argument %d: cannot pass an immutable value to a "
                            "mutable parameter",
                            i + 1);
    }
  }

done:
  c->generic_params = saved_gp;
  c->generic_param_count = saved_gpc;
}

void oak_check_fn_args(
    struct oak_compiler_t* c,
    const struct oak_ast_node_t* call,
    const struct oak_registered_fn_t* fn)
{
  if (fn->generic_param_count > 0 && fn->generic_def_index >= 0)
  {
    validate_generic_call_args(c, call, fn, 0);
    return;
  }
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
  if (m->generic_param_count > 0 && m->generic_def_index >= 0)
  {
    /* Native instance methods carry the implicit self at param_types slot 0;
     * static methods do not. */
    const int self_offset =
        (m->receiver_type_id != OAK_TYPE_VOID && !m->is_static) ? 1 : 0;
    validate_generic_call_args(c, call, m, self_offset);
    return;
  }
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
