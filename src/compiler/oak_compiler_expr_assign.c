#include "internal/oak_compiler.h"

void oak_compiler_compile_stmt_assignment(struct oak_compiler_t* c,
                                          const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* lhs = node->lhs;
  const struct oak_ast_node_t* rhs = node->rhs;

  if (lhs->kind == OAK_NODE_INDEX_ACCESS)
  {
    struct oak_type_t coll_ty;
    oakc_infer_type(c, lhs->lhs, &coll_ty);
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
      oakc_infer_type(c, lhs->rhs, &key_ty);
      if (oak_type_is_known(&key_ty))
      {
        const struct oak_type_t want_key = { .id = coll_ty.key_id };
        if (!oak_type_equal(&want_key, &key_ty))
        {
          oak_compiler_error_at(c,
                                lhs->rhs->token,
                                "map key must be of type '%s', got '%s'",
                                oakc_type_full_name(c, want_key),
                                oakc_type_full_name(c, key_ty));
          return;
        }
      }
    }

    struct oak_type_t val_ty;
    oakc_infer_type(c, rhs, &val_ty);
    if (oak_type_is_void(&val_ty))
    {
      oak_compiler_error_at(
          c, rhs->token, "cannot assign void to an indexed value");
      return;
    }
    if (oak_type_is_known(&val_ty))
    {
      const struct oak_type_t element_ty = { .id = coll_ty.id };
      if (!oak_type_equal(&element_ty, &val_ty))
      {
        oak_compiler_error_at(
            c,
            rhs->token,
            "cannot assign value of type '%s' to element of '%s' %s",
            oakc_type_full_name(c, val_ty),
            oakc_type_full_name(c, element_ty),
            coll_ty.kind == OAK_TYPE_KIND_MAP ? "map" : "array");
        return;
      }
    }

    oak_compiler_compile_node(c, lhs->lhs);
    oak_compiler_compile_node(c, lhs->rhs);
    oak_compiler_compile_node(c, rhs);
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
    if (recv->kind == OAK_NODE_MEMBER_ACCESS ||
        recv->kind == OAK_NODE_INDEX_ACCESS)
    {
      /* Chained field assignment stays rejected even when the root is
       * exclusive: an intermediate field may still alias a separate live
       * binding (e.g. `let mut c = new C { b : new B { a } }` where `a`
       * is a separate const binding), so writing through the chain can
       * surprise that holder. Lifting this requires recursive ownership
       * tracking, which is out of scope for the current borrow checker. */
      oak_compiler_error_at(c,
                            fname->token,
                            "cannot assign to field '%.*s' through a chained "
                            "access; bind the intermediate record to a mutable "
                            "variable first",
                            (int)oak_token_length(fname->token),
                            oak_token_text(fname->token));
      return;
    }
    const struct oak_registered_record_t* sd = null;
    const int idx = oakc_require_record_field(c, recv, fname, 1, &sd);
    if (idx < 0)
      return;

    if (!oak_compiler_expr_is_mutable_place(c, recv))
    {
      oak_compiler_error_at(c,
                            fname->token,
                            "cannot assign to field '%.*s' of immutable record",
                            (int)oak_token_length(fname->token),
                            oak_token_text(fname->token));
      return;
    }

    struct oak_type_t val_ty;
    oakc_infer_type(c, rhs, &val_ty);
    if (oak_type_is_void(&val_ty))
    {
      oak_compiler_error_at(c,
                            rhs->token ? rhs->token : fname->token,
                            "cannot assign void to a record field");
      return;
    }
    if (oak_type_is_known(&val_ty) &&
        !oak_type_equal(&sd->fields[idx].type, &val_ty))
    {
      oak_compiler_error_at(
          c,
          rhs->token ? rhs->token : fname->token,
          "cannot assign value of type '%s' to field '%s' of type '%s'",
          oakc_type_full_name(c, val_ty),
          sd->fields[idx].name,
          oakc_type_full_name(c, sd->fields[idx].type));
      return;
    }

    /* If the RHS names an exclusive binding, storing it into the record
     * field MOVES it: the source binding becomes unusable. Shared/fresh
     * RHSs are unaffected. */
    const int rhs_src_idx =
        oak_type_is_refcounted(&val_ty)
            ? oakc_ident_local(c, rhs)
            : -1;

    oak_compiler_compile_node(c, recv);
    oak_compiler_compile_node(c, rhs);
    oak_compiler_emit_op(c,
                         OAK_OP_SET_FIELD,
                         oak_compiler_loc_from_token(fname->token),
                         OAK_ARG_U8((u8)idx));

    if (rhs_src_idx >= 0)
    {
      struct oak_local_t* src = &c->scope.locals[rhs_src_idx];
      if (src->is_mutable && src->alive && src->frozen_by_slot < 0)
        src->alive = 0;
    }
    return;
  }

  const int slot = oakc_compile_assign_target(
      c, lhs, "assignment target must be a variable");
  if (slot < 0)
    return;

  oakc_reject_void(c, rhs);
  if (c->has_error)
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
  const int slot = oakc_compile_assign_target(
      c, lhs, "compound assignment target must be a variable");
  if (slot < 0)
    return;

  if (node->kind == OAK_NODE_STMT_ADD_ASSIGN &&
      oakc_is_int_literal(node->rhs, 1))
  {
    oak_compiler_emit_op(c,
                         OAK_OP_INC_LOCAL,
                         oak_compiler_loc_from_token(lhs->token),
                         OAK_ARG_U8((u8)slot));
    return;
  }
  if (node->kind == OAK_NODE_STMT_SUB_ASSIGN &&
      oakc_is_int_literal(node->rhs, 1))
  {
    oak_compiler_emit_op(c,
                         OAK_OP_DEC_LOCAL,
                         oak_compiler_loc_from_token(lhs->token),
                         OAK_ARG_U8((u8)slot));
    return;
  }

  oakc_reject_void(c, node->rhs);
  if (c->has_error)
    return;

  oak_compiler_emit_op(c,
                       OAK_OP_GET_LOCAL,
                       oak_compiler_loc_from_token(lhs->token),
                       OAK_ARG_U8((u8)slot));
  oak_compiler_compile_node(c, node->rhs);
  oak_compiler_emit_op(
      c,
      OAK_OP_BINARY,
      oak_compiler_loc_from_token(lhs->token),
      OAK_ARG_U8(oakc_binop_for_node(node->kind)));
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

  struct oak_type_t rhs_ty;
  oakc_infer_type(c, rhs, &rhs_ty);

  if (is_mutable && oak_type_is_refcounted(&rhs_ty) &&
      !oak_compiler_expr_is_mutable_place(c, rhs))
  {
    oak_compiler_error_at(c,
                          rhs->token,
                          "cannot create a mutable binding from an "
                          "immutable expression");
    return;
  }

  if (is_mutable && rhs && rhs->kind == OAK_NODE_EXPR_RECORD_LITERAL &&
      rhs->rhs)
  {
    struct oak_list_entry_t* pos;
    oak_list_for_each(pos, &rhs->rhs->children)
    {
      const struct oak_ast_node_t* field =
          oak_container_of(pos, struct oak_ast_node_t, link);
      if (field->kind != OAK_NODE_RECORD_LITERAL_FIELD || !field->rhs)
        continue;
      struct oak_type_t fty;
      oak_type_clear(&fty);
      oakc_infer_type(c, field->rhs, &fty);
      if (oak_type_is_refcounted(&fty) &&
          !oak_compiler_expr_is_mutable_place(c, field->rhs))
      {
        oak_compiler_error_at(
            c,
            field->lhs ? field->lhs->token : field->token,
            "cannot store immutable record reference in field '%.*s' of a "
            "mutable record; declare the source as 'mut' first",
            field->lhs ? (int)oak_token_length(field->lhs->token) : 0,
            field->lhs ? oak_token_text(field->lhs->token) : "");
        return;
      }
    }
  }

  oakc_reject_void(c, rhs);
  if (c->has_error)
    return;

  /* Capture the source binding (if RHS is a bare ident or `self`)
   * before compiling, so we can apply move/reborrow state changes
   * after the new local is in place. Only refcounted (heap) types
   * carry borrow state; value types (number/bool/enum) are copied. */
  const int src_local_idx =
      oak_type_is_refcounted(&rhs_ty)
          ? oakc_ident_local(c, rhs)
          : -1;

  oak_compiler_compile_node(c, rhs);
  const char* name = oak_token_text(ident->token);
  const usize name_len = oak_token_length(ident->token);
  oak_compiler_add_local(
      c, name, name_len, c->scope.stack_depth - 1, is_mutable, rhs_ty);

  if (src_local_idx >= 0)
  {
    struct oak_local_t* src = &c->scope.locals[src_local_idx];
    if (src->is_mutable && src->alive && src->frozen_by_slot < 0)
    {
      if (is_mutable)
      {
        /* Move: exclusivity transfers to the new binding. */
        src->alive = 0;
      }
      else
      {
        /* Shared reborrow: the source is frozen (read-only) for
         * the lifetime of the new binding. */
        const int new_slot = c->scope.locals[c->scope.local_count - 1].slot;
        src->frozen_by_slot = new_slot;
      }
    }
  }
}
