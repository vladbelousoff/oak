#include "internal/oak_compiler.h"
#include "internal/oak_compiler_modules.h"

/*
 * Why an expression has no value.
 *
 * Inference has a single channel for "no type": OAK_TYPE_VOID. A call to a
 * function that returns nothing lands there -- but so does every expression
 * the inferencer could not resolve: an undefined name, a field or a method
 * that does not exist, an enum variant that was never declared. Callers run
 * oak_reject_void *before* the expression is compiled, so it fires first and
 * the precise diagnostic codegen would have produced never happens; all the
 * user is told is "this expression has no value (void)".
 *
 * So before falling back to that sentence, re-derive the cause here, from the
 * same registries and in the same resolution order the codegen paths use, and
 * report it with the same wording. The generic message survives only for
 * expressions that really do evaluate to nothing.
 */

static int report_cause(oak_compiler_t* c, const oak_ast_node_t* expr);

/* The token to point at for `expr`. A node's `token` field is a union member
 * shared with `lhs`/`children`, so it may only be read on a TOKEN terminal;
 * composite nodes have to borrow the first token underneath them, otherwise
 * the diagnostic is printed with no source position at all. */
static const oak_token_t* expr_token(const oak_ast_node_t* expr)
{
  if (!expr)
    return OAK_NULL;
  if (oak_node_is_token_terminal(expr->kind))
    return expr->token;
  const usize n = oak_ast_node_child_count(expr);
  for (usize i = 0; i < n; ++i)
  {
    const oak_token_t* t = expr_token(oak_ast_node_child_at(expr, i));
    if (t)
      return t;
  }
  return OAK_NULL;
}

static int report_ident(oak_compiler_t* c, const oak_ast_node_t* expr)
{
  const char* name = oak_token_text(expr->token);

  /* A local or a function is a value that has a type: whatever made the
   * enclosing expression void, it was not this name. */
  if (oak_compiler_find_local(c, name, OAK_NULL) >= 0 || oak_find_fn(c, name))
    return 0;

  if (c->scope.fn_depth > 0 && oak_is_module_scope(c, name))
  {
    oak_compiler_error_at(
        c, expr->token, "'%s' is not visible here (module scope only)", name);
    return 1;
  }
  if (oak_is_enum_name(&c->enums, name))
  {
    oak_compiler_error_at(c,
                          expr->token,
                          "enum '%s' is a type, not a value; write "
                          "'%s.<variant>'",
                          name,
                          name);
    return 1;
  }
  if (oak_records_find(&c->records, name))
  {
    oak_compiler_error_at(c,
                          expr->token,
                          "record '%s' is a type, not a value; write "
                          "'new %s { ... }'",
                          name,
                          name);
    return 1;
  }
  if (oak_compiler_module_for_alias(c, name))
  {
    oak_compiler_error_at(
        c, expr->token, "'%s' is an imported module, not a value", name);
    return 1;
  }
  oak_compiler_error_at(c, expr->token, "undefined variable '%s'", name);
  return 1;
}

static int export_enum_has_variant(const oak_module_export_enum_t* exp,
                                   const char* variant)
{
  for (usize i = 0; i < oak_size(exp->variants); ++i)
  {
    const oak_module_export_enum_variant_t* v = oak_cget(exp->variants, i);
    if (strcmp(v->name, variant) == 0)
      return 1;
  }
  return 0;
}

static int report_member_access(oak_compiler_t* c, const oak_ast_node_t* expr)
{
  const oak_ast_node_t* recv = expr->lhs;
  const oak_ast_node_t* fname = expr->rhs;
  if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
    return 0;
  const char* member = oak_token_text(fname->token);

  /* alias.EnumName.Variant */
  {
    const oak_token_t* ename_tok = OAK_NULL;
    const oak_module_t* dep =
        oak_compiler_match_module_member(c, recv, &ename_tok);
    if (dep)
    {
      const char* ename = oak_token_text(ename_tok);
      if (oak_is_enum_name(&c->enums, ename))
      {
        if (!oak_enums_find_qualified(&c->enums, ename, member))
        {
          oak_compiler_error_at(
              c, fname->token, "enum '%s' has no variant '%s'", ename, member);
          return 1;
        }
        return 0;
      }
      /* An alias-only import registers nothing locally, so the variant has to
       * be checked against the module's export list. */
      const oak_module_export_enum_t* exp = oak_compiler_module_export_enum(
          c, oak_token_text(recv->lhs->token), ename, OAK_NULL);
      if (exp && !export_enum_has_variant(exp, member))
      {
        oak_compiler_error_at(c,
                              fname->token,
                              "enum '%s.%s' has no variant '%s'",
                              dep->dotted_name,
                              ename,
                              member);
        return 1;
      }
      return 0;
    }
  }

  /* EnumName.Variant */
  if (recv->kind == OAK_NODE_IDENT)
  {
    const char* rname = oak_token_text(recv->token);
    if (oak_is_enum_name(&c->enums, rname))
    {
      if (!oak_enums_find_qualified(&c->enums, rname, member))
      {
        oak_compiler_error_at(c,
                              fname->token,
                              "'%s' is not a variant of enum '%s'",
                              member,
                              rname);
        return 1;
      }
      return 0;
    }
  }

  if (report_cause(c, recv))
    return 1;

  /* Plain field access: this is exactly the check codegen runs, and it emits
   * the same diagnostic. */
  return oak_require_record_field(c, recv, fname, 0, OAK_NULL) < 0;
}

/* Instance-method lookup on a receiver whose type is known, mirroring the
 * dispatch order of oak_compile_method_call. */
static int report_instance_method(oak_compiler_t* c,
                                  const oak_ast_node_t* method,
                                  const oak_type_t* recv_ty,
                                  const char* mname)
{
  if (recv_ty->kind == OAK_TYPE_KIND_INTERFACE)
  {
    const oak_registered_interface_t* tr =
        oak_interface_find_by_id(&c->interfaces, recv_ty->id);
    if (tr && oak_interface_method_slot(tr, mname) < 0)
    {
      oak_compiler_error_at(c,
                            method->token,
                            "interface '%s' has no method '%s'",
                            tr->name,
                            mname);
      return 1;
    }
    return 0;
  }

  if (recv_ty->kind == OAK_TYPE_KIND_SCALAR)
  {
    const oak_registered_record_t* sd =
        oak_records_find_by_id(&c->records, recv_ty->id);
    if (sd)
    {
      if (oak_find_record_method(sd, mname, 0) ||
          oak_find_record_builtin_method(c, mname))
        return 0;
      oak_report_no_record_method(c, method->token, sd, mname);
      return 1;
    }
    if (recv_ty->id == OAK_TYPE_STRING && !oak_find_string_method(c, mname))
    {
      oak_compiler_error_at(
          c, method->token, "no method '%s' on string", mname);
      return 1;
    }
    if (recv_ty->id == OAK_TYPE_BOOL && !oak_find_bool_method(c, mname))
    {
      oak_compiler_error_at(c, method->token, "no method '%s' on bool", mname);
      return 1;
    }
    if (recv_ty->id == OAK_TYPE_NUMBER && !oak_find_number_method(c, mname))
    {
      oak_compiler_error_at(
          c, method->token, "no method '%s' on number", mname);
      return 1;
    }
    return 0;
  }

  const int is_map = recv_ty->kind == OAK_TYPE_KIND_MAP;
  if (is_map || recv_ty->kind == OAK_TYPE_KIND_ARRAY)
  {
    if (is_map ? oak_find_map_method(c, mname) != OAK_NULL
               : oak_find_array_method(c, mname) != OAK_NULL)
      return 0;
    oak_compiler_error_at(c,
                          method->token,
                          "no method '%s' on %s '%s'",
                          mname,
                          is_map ? "map" : "array of",
                          oak_type_full_name(c, *recv_ty));
    return 1;
  }
  return 0;
}

static int report_method_call(oak_compiler_t* c, const oak_ast_node_t* callee)
{
  const oak_ast_node_t* recv = callee->lhs;
  const oak_ast_node_t* method = callee->rhs;
  if (!recv || !method || method->kind != OAK_NODE_IDENT)
    return 0;
  const char* mname = oak_token_text(method->token);

  /* mod.Type.method() -- cross-module static method. Resolved by name, so it
   * has to be tried before the receiver is treated as a value. */
  if (recv->kind == OAK_NODE_MEMBER_ACCESS && recv->lhs && recv->rhs &&
      recv->lhs->kind == OAK_NODE_IDENT && recv->rhs->kind == OAK_NODE_IDENT)
  {
    const char* tname = oak_token_text(recv->rhs->token);
    const oak_module_t* dep = OAK_NULL;
    if (oak_compiler_module_export_record(
            c, oak_token_text(recv->lhs->token), tname, &dep))
    {
      const oak_registered_record_t* sd = oak_records_find(&c->records, tname);
      if (!oak_find_record_method(sd, mname, 1))
      {
        oak_compiler_error_at(c,
                              method->token,
                              "record '%s.%s' has no static method '%s'",
                              dep->dotted_name,
                              tname,
                              mname);
        return 1;
      }
      return 0;
    }
  }

  if (recv->kind == OAK_NODE_IDENT)
  {
    const char* rname = oak_token_text(recv->token);

    /* alias.fn() -- cross-module free function. */
    const oak_module_t* dep = OAK_NULL;
    const oak_module_export_fn_t* exp =
        oak_compiler_module_export_fn(c, rname, mname, &dep);
    if (dep)
    {
      if (!exp)
      {
        oak_compiler_error_at(c,
                              method->token,
                              "module '%s' has no exported function '%s'",
                              dep->dotted_name,
                              mname);
        return 1;
      }
      return 0;
    }

    /* Type.method() -- local static method. */
    oak_type_t local_ty;
    oak_type_clear(&local_ty);
    if (!oak_local_type_get(c, rname, &local_ty))
    {
      const oak_registered_record_t* sd = oak_records_find(&c->records, rname);
      if (sd)
      {
        if (oak_find_record_method(sd, mname, 1))
          return 0;
        if (oak_find_record_method(sd, mname, 0))
          oak_compiler_error_at(c,
                                method->token,
                                "'%s' is an instance method of record '%s'; "
                                "call it on a value, not on the type",
                                mname,
                                rname);
        else
          oak_compiler_error_at(c,
                                method->token,
                                "record '%s' has no static method '%s'",
                                rname,
                                mname);
        return 1;
      }
    }
  }

  if (report_cause(c, recv))
    return 1;

  oak_type_t recv_ty;
  oak_infer_type(c, recv, &recv_ty);
  if (!oak_type_is_known(&recv_ty))
    return 0;
  return report_instance_method(c, method, &recv_ty, mname);
}

static int report_fn_call(oak_compiler_t* c, const oak_ast_node_t* expr)
{
  const oak_ast_node_t* callee = oak_ast_node_child_at(expr, 0);
  if (!callee)
    return 0;
  if (callee->kind == OAK_NODE_MEMBER_ACCESS)
    return report_method_call(c, callee);
  if (callee->kind != OAK_NODE_IDENT)
    return 0;

  const char* name = oak_token_text(callee->token);
  if (oak_find_fn(c, name))
  {
    oak_compiler_error_at(c,
                          callee->token,
                          "function '%s' returns no value, so this expression "
                          "has no value (void)",
                          name);
    return 1;
  }
  if (oak_compiler_find_local(c, name, OAK_NULL) >= 0)
  {
    /* An indirect call through a fn value: the fn type carries no return
     * type, so the result can be discarded or passed on, never bound. */
    oak_compiler_error_at(c,
                          callee->token,
                          "cannot infer the result type of a call through the "
                          "fn value '%s'",
                          name);
    return 1;
  }
  oak_compiler_error_at(c, callee->token, "undefined function '%s'", name);
  return 1;
}

static int report_record_literal(oak_compiler_t* c, const oak_ast_node_t* expr)
{
  const oak_ast_node_t* path = expr->lhs;
  if (!path || path->kind != OAK_NODE_IMPORT_PATH)
    return 0;
  /* The record name is the last path segment: `Type` or `mod.Type`. */
  const oak_ast_node_t* type_seg = OAK_NULL;
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &path->children)
  {
    type_seg = OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
  }
  if (!type_seg)
    return 0;
  const char* name = oak_token_text(type_seg->token);
  if (oak_records_find(&c->records, name))
    return 0;
  oak_compiler_error_at(c, type_seg->token, "unknown record type '%s'", name);
  return 1;
}

static int report_index_access(oak_compiler_t* c, const oak_ast_node_t* expr)
{
  if (report_cause(c, expr->lhs))
    return 1;
  oak_type_t coll_ty;
  oak_infer_type(c, expr->lhs, &coll_ty);
  if (!oak_type_is_known(&coll_ty) || coll_ty.kind == OAK_TYPE_KIND_ARRAY ||
      coll_ty.kind == OAK_TYPE_KIND_MAP)
    return 0;
  oak_compiler_error_at(c,
                        expr_token(expr),
                        "cannot index a value of type '%s': indexing requires "
                        "an array or a map",
                        oak_type_full_name(c, coll_ty));
  return 1;
}

static int report_cause(oak_compiler_t* c, const oak_ast_node_t* expr)
{
  if (!expr)
    return 0;
  oak_type_t t;
  oak_infer_type(c, expr, &t);
  if (!oak_type_is_void(&t))
    return 0;

  switch (expr->kind)
  {
    case OAK_NODE_IDENT:
      return report_ident(c, expr);
    case OAK_NODE_SELF:
      if (oak_compiler_find_local(c, "self", OAK_NULL) < 0)
      {
        oak_compiler_error_at(
            c, expr->token, "'self' is only valid inside a method body");
        return 1;
      }
      return 0;
    case OAK_NODE_MEMBER_ACCESS:
      return report_member_access(c, expr);
    case OAK_NODE_FN_CALL:
      return report_fn_call(c, expr);
    case OAK_NODE_EXPR_RECORD_LITERAL:
      return report_record_literal(c, expr);
    case OAK_NODE_INDEX_ACCESS:
      return report_index_access(c, expr);
    default:
      return 0;
  }
}

void oak_reject_void(oak_compiler_t* c, const oak_ast_node_t* expr)
{
  if (!expr)
    return;
  oak_type_t t;
  oak_infer_type(c, expr, &t);
  if (!oak_type_is_void(&t))
    return;
  if (report_cause(c, expr))
    return;
  oak_compiler_error_at(
      c, expr_token(expr), "this expression has no value (void)");
}
