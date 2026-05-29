#include "internal/oak_compiler.h"

void oak_compiler_compile_stmt_assignment(struct oak_compiler_t* c,
                                          const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* lhs = node->lhs;
  const struct oak_ast_node_t* rhs = node->rhs;

  if (lhs->kind == OAK_NODE_INDEX_ACCESS)
  {
    if (!oak_compiler_expr_is_mutable_place(c, lhs->lhs))
    {
      oak_compiler_error_at(c,
                            lhs->token,
                            "cannot assign to indexed value of immutable "
                            "collection");
      return;
    }

    struct oak_type_t coll_ty;
    oak_infer_type(c, lhs->lhs, &coll_ty);
    if (coll_ty.kind == OAK_TYPE_KIND_SCALAR || !oak_type_is_known(&coll_ty))
    {
      oak_compiler_error_at(c,
                            lhs->lhs->token,
                            "indexed assignment requires a typed array or map");
      return;
    }

    if (coll_ty.kind == OAK_TYPE_KIND_MAP)
    {
      struct oak_type_t key_ty;
      oak_infer_type(c, lhs->rhs, &key_ty);
      if (oak_type_is_known(&key_ty))
      {
        const struct oak_type_t want_key = { .id = coll_ty.key_id };
        if (!oak_type_equal(&want_key, &key_ty))
        {
          oak_compiler_error_at(c,
                                lhs->rhs->token,
                                "map key must be of type '%s', got '%s'",
                                oak_type_full_name(c, want_key),
                                oak_type_full_name(c, key_ty));
          return;
        }
      }
    }

    struct oak_type_t val_ty;
    oak_infer_type(c, rhs, &val_ty);
    if (oak_type_is_void(&val_ty))
    {
      oak_compiler_error_at(
          c, rhs->token, "cannot assign void to an indexed value");
      return;
    }
    const struct oak_type_t element_ty = { .id = coll_ty.id };
    if (oak_type_is_known(&val_ty) &&
        !oak_type_accepts(&element_ty, &val_ty))
    {
      oak_compiler_error_at(
          c,
          rhs->token,
          "cannot assign value of type '%s' to element of '%s' %s",
          oak_type_full_name(c, val_ty),
          oak_type_full_name(c, element_ty),
          coll_ty.kind == OAK_TYPE_KIND_MAP ? "map" : "array");
      return;
    }

    if (oak_reject_immutable_ref_for_mutable_storage(
            c, rhs, val_ty, rhs->token, "indexed value"))
      return;

    oak_compiler_compile_node(c, lhs->lhs);
    oak_compiler_compile_node(c, lhs->rhs);
    oak_compiler_compile_node(c, rhs);
    oak_emit_weak_coerce(c, rhs, element_ty, OAK_LOC_SYNTHETIC);
    if (c->has_error)
      return;
    oak_compiler_emit_op(c, OAK_OP_SET_INDEX, OAK_LOC_SYNTHETIC);
    return;
  }

  if (lhs->kind == OAK_NODE_MEMBER_ACCESS)
  {
    const struct oak_ast_node_t* recv = lhs->lhs;
    const struct oak_ast_node_t* fname = lhs->rhs;
    if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, lhs->token, "field assignment requires 'expr.field = expr'");
      return;
    }
    const struct oak_registered_record_t* sd = null;
    const int idx = oak_require_record_field(c, recv, fname, 1, &sd);
    if (idx < 0)
      return;

    if (!oak_compiler_expr_is_mutable_place(c, recv))
    {
      oak_compiler_error_at(c,
                            fname->token,
                            "cannot assign to field '%.*s' of immutable record",
                            oak_token_size(fname->token),
                            oak_token_text(fname->token));
      return;
    }

    struct oak_type_t val_ty;
    oak_infer_type(c, rhs, &val_ty);
    if (oak_type_is_void(&val_ty))
    {
      oak_compiler_error_at(c,
                            rhs->token ? rhs->token : fname->token,
                            "cannot assign void to a record field");
      return;
    }
    if (oak_type_is_known(&val_ty) &&
        !oak_type_accepts(&sd->fields[idx].type, &val_ty))
    {
      oak_compiler_error_at(
          c,
          rhs->token ? rhs->token : fname->token,
          "cannot assign value of type '%s' to field '%s' of type '%s'",
          oak_type_full_name(c, val_ty),
          sd->fields[idx].name,
          oak_type_full_name(c, sd->fields[idx].type));
      return;
    }

    if (oak_reject_immutable_ref_for_mutable_storage(
            c, rhs, val_ty, rhs->token ? rhs->token : fname->token, "field"))
      return;

    oak_compiler_compile_node(c, recv);
    oak_compiler_compile_node(c, rhs);
    oak_emit_trait_coerce(c,
                           rhs,
                           sd->fields[idx].type,
                           oak_compiler_loc_from_token(fname->token));
    if (c->has_error)
      return;
    oak_emit_weak_coerce(c,
                          rhs,
                          sd->fields[idx].type,
                          oak_compiler_loc_from_token(fname->token));
    if (c->has_error)
      return;
    oak_compiler_emit_op(c,
                         OAK_OP_SET_FIELD,
                         oak_compiler_loc_from_token(fname->token),
                         OAK_ARG_U8((u8)idx));
    return;
  }

  const int slot = oak_compile_assign_target(
      c, lhs, "assignment target must be a variable");
  if (slot < 0)
    return;

  oak_reject_void(c, rhs);
  if (c->has_error)
    return;

  struct oak_type_t rhs_ty;
  oak_infer_type(c, rhs, &rhs_ty);
  const int target_idx = oak_local_at_slot(c, slot);
  if (target_idx >= 0 &&
      oak_compiler_type_is_refcounted(c, &c->scope.locals[target_idx].type) &&
      oak_reject_immutable_ref_for_mutable_storage(
          c, rhs, rhs_ty, rhs->token, "binding"))
    return;

  oak_compiler_compile_node(c, rhs);
  oak_compiler_emit_op(c,
                       OAK_OP_SET_LOCAL,
                       oak_compiler_loc_from_token(lhs->token),
                       OAK_ARG_U8((u8)slot));
}

void oak_compiler_compile_compound_assign(struct oak_compiler_t* c,
                                          const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* lhs = node->lhs;

  if (lhs->kind == OAK_NODE_INDEX_ACCESS)
  {
    if (!oak_compiler_expr_is_mutable_place(c, lhs->lhs))
    {
      oak_compiler_error_at(c,
                            lhs->token,
                            "cannot assign to indexed value of immutable "
                            "collection");
      return;
    }

    struct oak_type_t coll_ty;
    oak_infer_type(c, lhs->lhs, &coll_ty);
    if (coll_ty.kind == OAK_TYPE_KIND_SCALAR || !oak_type_is_known(&coll_ty))
    {
      oak_compiler_error_at(c,
                            lhs->lhs->token,
                            "indexed assignment requires a typed array or map");
      return;
    }

    if (coll_ty.kind == OAK_TYPE_KIND_MAP)
    {
      struct oak_type_t key_ty;
      oak_infer_type(c, lhs->rhs, &key_ty);
      if (oak_type_is_known(&key_ty))
      {
        const struct oak_type_t want_key = { .id = coll_ty.key_id };
        if (!oak_type_equal(&want_key, &key_ty))
        {
          oak_compiler_error_at(c,
                                lhs->rhs->token,
                                "map key must be of type '%s', got '%s'",
                                oak_type_full_name(c, want_key),
                                oak_type_full_name(c, key_ty));
          return;
        }
      }
    }

    oak_reject_void(c, node->rhs);
    if (c->has_error)
      return;

    oak_compiler_compile_node(c, lhs->lhs);
    oak_compiler_compile_node(c, lhs->rhs);
    oak_compiler_compile_node(c, lhs->lhs);
    oak_compiler_compile_node(c, lhs->rhs);
    oak_compiler_emit_op(c, OAK_OP_GET_INDEX, OAK_LOC_SYNTHETIC);
    oak_compiler_compile_node(c, node->rhs);
    oak_compiler_emit_op(c,
                         oak_binop_for_node(node->kind),
                         oak_compiler_loc_from_token(lhs->token));
    const struct oak_type_t element_ty = { .id = coll_ty.id };
    oak_emit_weak_coerce(c, node->rhs, element_ty, OAK_LOC_SYNTHETIC);
    if (c->has_error)
      return;
    oak_compiler_emit_op(c, OAK_OP_SET_INDEX, OAK_LOC_SYNTHETIC);
    return;
  }

  if (lhs->kind == OAK_NODE_MEMBER_ACCESS)
  {
    const struct oak_ast_node_t* recv = lhs->lhs;
    const struct oak_ast_node_t* fname = lhs->rhs;
    if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, lhs->token, "field assignment requires 'expr.field = expr'");
      return;
    }

    const struct oak_registered_record_t* sd = null;
    const int idx = oak_require_record_field(c, recv, fname, 1, &sd);
    if (idx < 0)
      return;

    if (!oak_compiler_expr_is_mutable_place(c, recv))
    {
      oak_compiler_error_at(c,
                            fname->token,
                            "cannot assign to field '%.*s' of immutable record",
                            oak_token_size(fname->token),
                            oak_token_text(fname->token));
      return;
    }

    oak_reject_void(c, node->rhs);
    if (c->has_error)
      return;

    oak_compiler_compile_node(c, recv);
    oak_compiler_compile_node(c, recv);
    oak_compiler_emit_op(c,
                         OAK_OP_GET_FIELD,
                         oak_compiler_loc_from_token(fname->token),
                         OAK_ARG_U8((u8)idx));
    oak_compiler_compile_node(c, node->rhs);
    oak_compiler_emit_op(c,
                         oak_binop_for_node(node->kind),
                         oak_compiler_loc_from_token(lhs->token));
    oak_emit_trait_coerce(c,
                           node->rhs,
                           sd->fields[idx].type,
                           oak_compiler_loc_from_token(fname->token));
    if (c->has_error)
      return;
    oak_emit_weak_coerce(c,
                          node->rhs,
                          sd->fields[idx].type,
                          oak_compiler_loc_from_token(fname->token));
    if (c->has_error)
      return;
    oak_compiler_emit_op(c,
                         OAK_OP_SET_FIELD,
                         oak_compiler_loc_from_token(fname->token),
                         OAK_ARG_U8((u8)idx));
    return;
  }

  const int slot = oak_compile_assign_target(
      c, lhs, "compound assignment target must be a variable");
  if (slot < 0)
    return;

  if (node->kind == OAK_NODE_STMT_ADD_ASSIGN &&
      oak_is_int_literal(node->rhs, 1))
  {
    oak_compiler_emit_op(c,
                         OAK_OP_INC_LOCAL,
                         oak_compiler_loc_from_token(lhs->token),
                         OAK_ARG_U8((u8)slot));
    return;
  }
  if (node->kind == OAK_NODE_STMT_SUB_ASSIGN &&
      oak_is_int_literal(node->rhs, 1))
  {
    oak_compiler_emit_op(c,
                         OAK_OP_DEC_LOCAL,
                         oak_compiler_loc_from_token(lhs->token),
                         OAK_ARG_U8((u8)slot));
    return;
  }

  oak_reject_void(c, node->rhs);
  if (c->has_error)
    return;

  oak_compiler_emit_op(c,
                       OAK_OP_GET_LOCAL,
                       oak_compiler_loc_from_token(lhs->token),
                       OAK_ARG_U8((u8)slot));
  oak_compiler_compile_node(c, node->rhs);
  oak_compiler_emit_op(c,
                       oak_binop_for_node(node->kind),
                       oak_compiler_loc_from_token(lhs->token));
  oak_compiler_emit_op(c,
                       OAK_OP_SET_LOCAL,
                       oak_compiler_loc_from_token(lhs->token),
                       OAK_ARG_U8((u8)slot));
}

void oak_compiler_compile_let_assignment(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* node)
{
  /* STMT_LET_ASSIGNMENT is BINARY: lhs = MUT_KEYWORD? (non-null iff
   * mutable), rhs = STMT_ASSIGNMENT. */
  const int is_mutable = node->lhs != null;
  const struct oak_ast_node_t* assign = node->rhs;

  if (!assign || assign->kind != OAK_NODE_STMT_ASSIGNMENT)
  {
    oak_compiler_error_at(c, null, "malformed 'let' statement");
    return;
  }

  const struct oak_ast_node_t* ident = assign->lhs;
  const struct oak_ast_node_t* rhs = assign->rhs;

  oak_reject_void(c, rhs);
  if (c->has_error)
    return;

  struct oak_type_t rhs_ty;
  oak_infer_type(c, rhs, &rhs_ty);

  if (is_mutable && oak_reject_immutable_ref_for_mutable_storage(
                        c, rhs, rhs_ty, rhs->token, "binding"))
    return;

  oak_compiler_compile_node(c, rhs);
  const char* name = oak_token_text(ident->token);
  const int name_len = oak_token_size(ident->token);
  oak_compiler_add_local(
      c, name, name_len, c->scope.stack_depth - 1, is_mutable, rhs_ty);
}
