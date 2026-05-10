#include "internal/oak_compiler.h"

int oakc_is_module_scope(const struct oak_compiler_t* c,
                                      const char* name,
                                      const usize len)
{
  return oak_htable_get(&c->module_scope_names, name, len) >= 0;
}

int oak_compiler_find_local(const struct oak_compiler_t* c,
                            const char* name,
                            const usize length,
                            int* out_is_mutable)
{
  for (int i = c->scope.local_count - 1; i >= 0; --i)
  {
    const struct oak_local_t* local = &c->scope.locals[i];
    if (oak_name_eq(local->name, local->length, name, length))
    {
      if (out_is_mutable)
        *out_is_mutable = local->is_mutable;
      return local->slot;
    }
  }

  return -1;
}

void oak_compiler_add_local(struct oak_compiler_t* c,
                            const char* name,
                            const usize length,
                            const int slot,
                            const int is_mutable,
                            const struct oak_type_t type)
{
  if (c->scope.local_count >= OAK_MAX_LOCALS)
  {
    oak_compiler_error_at(
        c, null, "too many local variables (max %d)", OAK_MAX_LOCALS);
    return;
  }
  struct oak_local_t* local = &c->scope.locals[c->scope.local_count++];
  local->name = name;
  local->length = length;
  local->slot = slot;
  local->is_mutable = is_mutable;
  local->depth = c->scope.scope_depth;
  local->type = type;
  local->alive = 1;
  local->frozen_by_slot = -1;

  oak_chunk_add_debug_local(c->chunk, slot, name, length);
}

void oak_compiler_begin_scope(struct oak_compiler_t* c)
{
  c->scope.scope_depth++;
}

void oak_compiler_end_scope(struct oak_compiler_t* c)
{
  int pops = 0;
  while (c->scope.local_count > 0 &&
         c->scope.locals[c->scope.local_count - 1].depth ==
             c->scope.scope_depth)
  {
    /* The departing local may have been freezing some other (still-in-scope)
     * binding via shared reborrow. Release that freeze so the source becomes
     * writable again after this scope ends. */
    const int leaving_slot = c->scope.locals[c->scope.local_count - 1].slot;
    for (int i = 0; i < c->scope.local_count - 1; ++i)
    {
      if (c->scope.locals[i].frozen_by_slot == leaving_slot)
        c->scope.locals[i].frozen_by_slot = -1;
    }
    pops++;
    c->scope.local_count--;
  }
  oak_compiler_emit_pops(c, pops, OAK_LOC_SYNTHETIC);
  c->scope.scope_depth--;
}

int oakc_local_at_slot(const struct oak_compiler_t* c, int slot)
{
  for (int i = c->scope.local_count - 1; i >= 0; --i)
  {
    if (c->scope.locals[i].slot == slot)
      return i;
  }
  return -1;
}

int oakc_ident_local(const struct oak_compiler_t* c,
                                            const struct oak_ast_node_t* expr)
{
  if (!expr)
    return -1;
  const char* name = null;
  usize len = 0;
  if (expr->kind == OAK_NODE_IDENT)
  {
    name = oak_token_text(expr->token);
    len = oak_token_length(expr->token);
  }
  else if (expr->kind == OAK_NODE_SELF)
  {
    name = "self";
    len = 4u;
  }
  else
  {
    return -1;
  }
  for (int i = c->scope.local_count - 1; i >= 0; --i)
  {
    const struct oak_local_t* local = &c->scope.locals[i];
    if (oak_name_eq(local->name, local->length, name, len))
      return i;
  }
  return -1;
}

int oakc_place_root_local(const struct oak_compiler_t* c,
                                            const struct oak_ast_node_t* expr)
{
  while (expr && (expr->kind == OAK_NODE_MEMBER_ACCESS ||
                  expr->kind == OAK_NODE_INDEX_ACCESS))
    expr = expr->lhs;
  return oakc_ident_local(c, expr);
}

int oakc_compile_assign_target(struct oak_compiler_t* c,
                                       const struct oak_ast_node_t* lhs,
                                       const char* non_ident_msg)
{
  if (lhs->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(c, lhs->token, "%s", non_ident_msg);
    return -1;
  }
  const char* name = oak_token_text(lhs->token);
  const usize name_len = oak_token_length(lhs->token);
  int is_mutable = 0;
  const int slot = oak_compiler_find_local(c, name, name_len, &is_mutable);
  if (slot < 0)
  {
    if (c->scope.fn_depth > 0 &&
        oakc_is_module_scope(c, name, name_len))
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
  const int li = oakc_local_at_slot(c, slot);
  if (li >= 0)
  {
    const struct oak_local_t* l = &c->scope.locals[li];
    if (!l->alive)
    {
      oak_compiler_error_at(
          c, lhs->token, "cannot assign to '%s': value was moved out", name);
      return -1;
    }
    if (l->frozen_by_slot >= 0)
    {
      oak_compiler_error_at(
          c,
          lhs->token,
          "cannot assign to '%s': it is currently borrowed (read-only)",
          name);
      return -1;
    }
  }
  return slot;
}

int oak_compiler_expr_is_mutable_place(const struct oak_compiler_t* c,
                                       const struct oak_ast_node_t* expr)
{
  if (!expr)
    return 1;
  if (expr->kind == OAK_NODE_IDENT || expr->kind == OAK_NODE_SELF)
  {
    const int idx = oakc_ident_local(c, expr);
    if (idx < 0)
      return 0;
    const struct oak_local_t* l = &c->scope.locals[idx];
    /* A binding is usable as a mutable place iff it was declared `mut`,
     * has not been moved out, and is not currently shared-reborrowed
     * (a shared reborrow freezes the source for write). */
    return l->is_mutable && l->alive && l->frozen_by_slot < 0;
  }
  if (expr->kind == OAK_NODE_MEMBER_ACCESS ||
      expr->kind == OAK_NODE_INDEX_ACCESS)
    return oak_compiler_expr_is_mutable_place(c, expr->lhs);
  return 1;
}
