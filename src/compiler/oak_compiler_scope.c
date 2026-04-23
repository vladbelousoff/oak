#include "oak_compiler_internal.h"

int oak_compiler_find_local(const struct oak_compiler_t* c,
                            const char* name,
                            const usize length,
                            int* out_is_mutable)
{
  for (int i = c->local_count - 1; i >= 0; --i)
  {
    const struct oak_local_t* local = &c->locals[i];
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
  if (c->local_count >= OAK_MAX_LOCALS)
  {
    oak_compiler_error_at(
        c, null, "too many local variables (max %d)", OAK_MAX_LOCALS);
    return;
  }
  struct oak_local_t* local = &c->locals[c->local_count++];
  local->name = name;
  local->length = length;
  local->slot = slot;
  local->is_mutable = is_mutable;
  local->depth = c->scope_depth;
  local->type = type;

  oak_chunk_add_debug_local(c->chunk, slot, name, length);
}

void oak_compiler_begin_scope(struct oak_compiler_t* c)
{
  c->scope_depth++;
}

void oak_compiler_end_scope(struct oak_compiler_t* c)
{
  int pops = 0;
  while (c->local_count > 0
         && c->locals[c->local_count - 1].depth == c->scope_depth)
  {
    pops++;
    c->local_count--;
  }
  oak_compiler_emit_pops(c, pops, OAK_LOC_SYNTHETIC);
  c->scope_depth--;
}

int oak_compiler_compile_assign_target(struct oak_compiler_t* c,
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

int oak_compiler_expr_is_mutable_place(const struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* expr)
{
  if (!expr) return 1;
  if (expr->kind == OAK_NODE_IDENT)
  {
    int is_mutable = 0;
    oak_compiler_find_local(
        c, oak_token_text(expr->token), oak_token_length(expr->token),
        &is_mutable);
    return is_mutable;
  }
  if (expr->kind == OAK_NODE_SELF)
  {
    int is_mutable = 0;
    oak_compiler_find_local(c, "self", 4u, &is_mutable);
    return is_mutable;
  }
  if (expr->kind == OAK_NODE_MEMBER_ACCESS ||
      expr->kind == OAK_NODE_INDEX_ACCESS)
    return oak_compiler_expr_is_mutable_place(c, expr->lhs);
  return 1;
}
