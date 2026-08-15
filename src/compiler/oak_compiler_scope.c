#include "internal/oak_compiler.h"

int oak_is_module_scope(const oak_compiler_t* c,
                        const char* name)
{
  return oak_contains_str(c->module_scope_names, name);
}

int oak_compiler_find_local(const oak_compiler_t* c,
                            const char* name,
                            int* out_is_mutable)
{
  for (int i = c->scope.local_count - 1; i >= 0; --i)
  {
    const oak_local_t* local = &c->scope.locals[i];
    if (oak_name_eq(local->name, name))
    {
      if (out_is_mutable)
        *out_is_mutable = local->is_mutable;
      return local->slot;
    }
  }

  return -1;
}

void oak_compiler_add_local(oak_compiler_t* c,
                            const char* name,
                            const int slot,
                            const int is_mutable,
                            const oak_type_t type)
{
  if (c->scope.local_count >= OAK_MAX_LOCALS)
  {
    oak_compiler_error_at(
        c, OAK_NULL, "too many local variables (max %d)", OAK_MAX_LOCALS);
    return;
  }
  oak_local_t* local = &c->scope.locals[c->scope.local_count++];
  local->name = name;
  local->slot = slot;
  local->is_mutable = is_mutable;
  local->depth = c->scope.scope_depth;
  local->type = type;

  oak_chunk_add_debug_local(c->chunk, slot, name);
}

void oak_compiler_begin_scope(oak_compiler_t* c)
{
  c->scope.scope_depth++;
}

void oak_compiler_end_scope(oak_compiler_t* c)
{
  int pops = 0;
  while (c->scope.local_count > 0 &&
         c->scope.locals[c->scope.local_count - 1].depth ==
             c->scope.scope_depth)
  {
    const int slot = c->scope.locals[c->scope.local_count - 1].slot;
    oak_chunk_end_debug_local(c->chunk, slot);
    pops++;
    c->scope.local_count--;
  }
  oak_compiler_emit_pops(c, pops, OAK_LOC_SYNTHETIC);
  c->scope.scope_depth--;
}

int oak_local_at_slot(const oak_compiler_t* c, int slot)
{
  for (int i = c->scope.local_count - 1; i >= 0; --i)
  {
    if (c->scope.locals[i].slot == slot)
      return i;
  }
  return -1;
}

int oak_ident_local(const oak_compiler_t* c,
                                            const oak_ast_node_t* expr)
{
  if (!expr)
    return -1;
  const char* name = OAK_NULL;
  if (expr->kind == OAK_NODE_IDENT)
  {
    name = oak_token_text(expr->token);
  }
  else if (expr->kind == OAK_NODE_SELF)
  {
    name = "self";
  }
  else
  {
    return -1;
  }
  for (int i = c->scope.local_count - 1; i >= 0; --i)
  {
    const oak_local_t* local = &c->scope.locals[i];
    if (oak_name_eq(local->name, name))
      return i;
  }
  return -1;
}

int oak_place_root_local(const oak_compiler_t* c,
                                            const oak_ast_node_t* expr)
{
  while (expr && (expr->kind == OAK_NODE_MEMBER_ACCESS ||
                  expr->kind == OAK_NODE_INDEX_ACCESS))
    expr = expr->lhs;
  return oak_ident_local(c, expr);
}

int oak_expr_is_reference_place(const oak_compiler_t* c,
                                 const oak_ast_node_t* expr)
{
  return oak_place_root_local(c, expr) >= 0;
}

int oak_compile_assign_target(oak_compiler_t* c,
                                       const oak_ast_node_t* lhs,
                                       const char* non_ident_msg)
{
  if (lhs->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(c, lhs->token, "%s", non_ident_msg);
    return -1;
  }
  const char* name = oak_token_text(lhs->token);
  int is_mutable = 0;
  const int slot = oak_compiler_find_local(c, name, &is_mutable);
  if (slot < 0)
  {
    if (c->scope.fn_depth > 0 &&
        oak_is_module_scope(c, name))
    {
      oak_compiler_error_at(
          c,
          lhs->token,
          "cannot assign to '%s': not visible here (module scope only)",
          name);
      return -1;
    }
    oak_compiler_error_at(c, lhs->token, "undefined variable '%s'", name);
    return -1;
  }
  if (!is_mutable)
  {
    oak_compiler_error_at(
        c, lhs->token, "cannot assign to immutable variable '%s'", name);
    return -1;
  }
  return slot;
}

int oak_compiler_expr_is_mutable_place(const oak_compiler_t* c,
                                       const oak_ast_node_t* expr)
{
  if (!expr)
    return 1;
  if (expr->kind == OAK_NODE_IDENT || expr->kind == OAK_NODE_SELF)
  {
    const int idx = oak_ident_local(c, expr);
    if (idx < 0)
      return 0;
    const oak_local_t* l = &c->scope.locals[idx];
    return l->is_mutable;
  }
  if (expr->kind == OAK_NODE_MEMBER_ACCESS ||
      expr->kind == OAK_NODE_INDEX_ACCESS)
    return oak_compiler_expr_is_mutable_place(c, expr->lhs);
  return 1;
}

static int
oak_reject_immutable_refs_inside_literal(oak_compiler_t* c,
                                          const oak_ast_node_t* expr,
                                          const char* target)
{
  if (!expr)
    return 0;

  if (expr->kind == OAK_NODE_EXPR_RECORD_LITERAL && expr->rhs)
  {
    oak_list_entry_t* pos;
    OAK_LIST_FOR_EACH(pos, &expr->rhs->children)
    {
      const oak_ast_node_t* field =
          OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
      if (field->kind != OAK_NODE_RECORD_LITERAL_FIELD || !field->rhs)
        continue;
      oak_type_t fty;
      oak_type_clear(&fty);
      oak_infer_type(c, field->rhs, &fty);
      if (oak_reject_immutable_ref_for_mutable_storage(
              c,
              field->rhs,
              fty,
              field->lhs ? field->lhs->token : field->token,
              target))
        return 1;
    }
    return 0;
  }

  if (expr->kind == OAK_NODE_EXPR_ARRAY_LITERAL)
  {
    oak_list_entry_t* pos;
    OAK_LIST_FOR_EACH(pos, &expr->children)
    {
      const oak_ast_node_t* wrap =
          OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
      const oak_ast_node_t* elem =
          wrap->kind == OAK_NODE_ARRAY_LITERAL_ELEMENT ? wrap->child : wrap;
      oak_type_t ety;
      oak_type_clear(&ety);
      oak_infer_type(c, elem, &ety);
      if (oak_reject_immutable_ref_for_mutable_storage(
              c, elem, ety, elem ? elem->token : wrap->token, target))
        return 1;
    }
    return 0;
  }

  if (expr->kind == OAK_NODE_EXPR_MAP_LITERAL)
  {
    const oak_ast_node_t* first = expr->lhs;
    if (first && first->kind == OAK_NODE_MAP_LITERAL_ENTRY && first->rhs)
    {
      oak_type_t vty;
      oak_type_clear(&vty);
      oak_infer_type(c, first->rhs, &vty);
      if (oak_reject_immutable_ref_for_mutable_storage(
              c, first->rhs, vty, first->rhs->token, target))
        return 1;
    }
    if (expr->rhs)
    {
      oak_list_entry_t* pos;
      OAK_LIST_FOR_EACH(pos, &expr->rhs->children)
      {
        const oak_ast_node_t* entry =
            OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
        if (entry->kind != OAK_NODE_MAP_LITERAL_ENTRY || !entry->rhs)
          continue;
        oak_type_t vty;
        oak_type_clear(&vty);
        oak_infer_type(c, entry->rhs, &vty);
        if (oak_reject_immutable_ref_for_mutable_storage(
                c, entry->rhs, vty, entry->rhs->token, target))
          return 1;
      }
    }
  }

  return 0;
}

int oak_reject_immutable_ref_for_mutable_storage(
    oak_compiler_t* c,
    const oak_ast_node_t* expr,
    oak_type_t ty,
    const oak_token_t* err_tok,
    const char* target)
{
  if (!oak_compiler_type_is_refcounted(c, &ty))
    return 0;
  if (oak_reject_immutable_refs_inside_literal(c, expr, target))
    return 1;
  if (oak_compiler_expr_is_mutable_place(c, expr))
    return 0;
  oak_compiler_error_at(c,
                        err_tok ? err_tok : (expr ? expr->token : OAK_NULL),
                        "cannot store immutable reference in mutable %s; "
                        "declare the source as 'mut' first",
                        target);
  return 1;
}
