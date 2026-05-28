#include "internal/oak_compiler.h"

void oak_compile_for_from(struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* node)
{
  oak_assert(oak_child_count(node) >= 4u);

  struct oak_list_entry_t* pos = node->children.next;
  const struct oak_ast_node_t* ident =
      oak_container_of(pos, struct oak_ast_node_t, link);
  pos = pos->next;
  const struct oak_ast_node_t* from_expr =
      oak_container_of(pos, struct oak_ast_node_t, link);
  pos = pos->next;
  const struct oak_ast_node_t* to_expr =
      oak_container_of(pos, struct oak_ast_node_t, link);
  pos = pos->next;
  const struct oak_ast_node_t* body =
      oak_container_of(pos, struct oak_ast_node_t, link);

  oak_compiler_begin_scope(c);

  oak_reject_void(c, from_expr);
  if (c->has_error)
  {
    oak_compiler_end_scope(c);
    return;
  }
  oak_reject_void(c, to_expr);
  if (c->has_error)
  {
    oak_compiler_end_scope(c);
    return;
  }

  struct oak_type_t from_ty;
  oak_infer_type(c, from_expr, &from_ty);
  if (!oak_type_is_known(&from_ty))
    from_ty.id = OAK_TYPE_NUMBER;

  oak_compiler_compile_node(c, from_expr);
  const int loop_var_slot = c->scope.stack_depth - 1;
  oak_compiler_add_local(c,
                         oak_token_text(ident->token),
                         oak_token_size(ident->token),
                         loop_var_slot,
                         1,
                         from_ty);

  struct oak_type_t to_ty;
  oak_infer_type(c, to_expr, &to_ty);
  if (!oak_type_is_known(&to_ty))
    to_ty.id = OAK_TYPE_NUMBER;

  oak_compiler_compile_node(c, to_expr);
  const int limit_slot = c->scope.stack_depth - 1;
  oak_compiler_add_local(c, "", 0, limit_slot, 0, to_ty);

  struct oak_loop_frame_t loop = {
    .enclosing = c->scope.current_loop,
    .loop_start = c->chunk->count,
    .exit_depth = c->scope.stack_depth - 2,
    .continue_depth = c->scope.stack_depth,
    .break_jumps = null,
    .break_count = 0,
    .break_capacity = 0,
    .continue_jumps = null,
    .continue_count = 0,
    .continue_capacity = 0,
  };

  c->scope.current_loop = &loop;

  {
    const struct oak_code_loc_t ident_loc =
        oak_compiler_loc_from_token(ident->token);
    oak_compiler_emit_op(
        c, OAK_OP_GET_LOCAL_GET_LOCAL, ident_loc,
        OAK_ARG_U8((u8)loop_var_slot), OAK_ARG_U8((u8)limit_slot));
    const usize exit_jump =
        oak_compiler_emit_jump(c, OAK_OP_LESS_JUMP_IF_FALSE, ident_loc);

    const int merge_stack_depth = c->scope.stack_depth;

    oak_compiler_compile_block(c, body);
    c->scope.stack_depth = merge_stack_depth;

    oak_compiler_patch_jumps(c, loop.continue_jumps, loop.continue_count);

    {
      oak_compiler_emit_byte(c, OAK_OP_INC_LOCAL_LOOP, ident_loc);
      oak_compiler_emit_byte(c, (u8)loop_var_slot, ident_loc);
      const usize jump = c->chunk->count - loop.loop_start + 2;
      if (jump > 0xFFFFu)
      {
        oak_compiler_error_at(
            c, null,
            "loop distance %zu exceeds 16-bit limit (max 65535)", jump);
        oak_compiler_emit_byte(c, 0, ident_loc);
        oak_compiler_emit_byte(c, 0, ident_loc);
      }
      else
      {
        oak_compiler_emit_byte(c, (u8)(jump >> 8), ident_loc);
        oak_compiler_emit_byte(c, (u8)(jump), ident_loc);
      }
    }
    oak_compiler_patch_jump(c, exit_jump);
  }

  oak_compiler_end_scope(c);

  oak_compiler_patch_jumps(c, loop.break_jumps, loop.break_count);

  c->scope.current_loop = loop.enclosing;
  oak_dynarr_free(c->allocator, &loop.break_jumps, &loop.break_count, &loop.break_capacity);
  oak_dynarr_free(c->allocator,
      &loop.continue_jumps, &loop.continue_count, &loop.continue_capacity);
}

/* Iterates over an array or map.
 *
 *   for v in arr        // v = element value
 *   for i, v in arr     // i = index (0-based), v = element value
 *   for k in map        // k = key
 *   for k, v in map     // k = key, v = value
 *
 * The collection is evaluated once. Iteration is positional: snapshotting
 * the length up-front means inserts during a map iteration won't be seen,
 * and deletes can shift remaining entries (the map stores them densely). */

static void for_in_init_hidden_state(struct oak_compiler_t* c,
                                     const struct oak_code_loc_t loc,
                                     const struct oak_method_binding_t* len_m,
                                     const int coll_slot,
                                     int* out_idx_slot,
                                     int* out_limit_slot)
{
  oak_compiler_emit_constant(
      c, oak_compiler_intern_constant(c, OAK_VALUE_I32(0)), loc);
  *out_idx_slot = c->scope.stack_depth - 1;
  const struct oak_type_t num_ty = { .id = OAK_TYPE_NUMBER };
  oak_compiler_add_local(c, "$i", 0, *out_idx_slot, 1, num_ty);

  oak_compiler_emit_constant(c, len_m->const_idx, loc);
  oak_compiler_emit_op(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)coll_slot));
  oak_compiler_emit_op(c, OAK_OP_CALL, loc, OAK_ARG_U8((u8)len_m->total_arity));
  c->scope.stack_depth -= len_m->total_arity;
  *out_limit_slot = c->scope.stack_depth - 1;
  oak_compiler_add_local(c, "$n", 0, *out_limit_slot, 0, num_ty);
}

static void for_in_bind_loop_idents(struct oak_compiler_t* c,
                                    const struct oak_code_loc_t loc,
                                    const struct oak_type_t* coll_ty,
                                    const int coll_slot,
                                    const int idx_slot,
                                    const struct oak_ast_node_t* k_ident,
                                    const struct oak_ast_node_t* v_ident)
{
  if (k_ident)
  {
    if (coll_ty->kind == OAK_TYPE_KIND_MAP)
    {
      oak_compiler_emit_op(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)coll_slot));
      oak_compiler_emit_op(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)idx_slot));
      oak_compiler_emit_op(c, OAK_OP_MAP_KEY_AT, loc);
      const struct oak_type_t key_ty = { .id = coll_ty->key_id };
      oak_compiler_add_local(c,
                             oak_token_text(k_ident->token),
                             oak_token_size(k_ident->token),
                             c->scope.stack_depth - 1,
                             0,
                             key_ty);
    }
    else
    {
      oak_compiler_emit_op(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)idx_slot));
      const struct oak_type_t num_ty = { .id = OAK_TYPE_NUMBER };
      oak_compiler_add_local(c,
                             oak_token_text(k_ident->token),
                             oak_token_size(k_ident->token),
                             c->scope.stack_depth - 1,
                             0,
                             num_ty);
    }
  }
  if (v_ident)
  {
    oak_compiler_emit_op(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)coll_slot));
    oak_compiler_emit_op(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)idx_slot));
    oak_compiler_emit_op(c,
                         coll_ty->kind == OAK_TYPE_KIND_MAP ? OAK_OP_MAP_VAL_AT
                                                            : OAK_OP_GET_INDEX,
                         loc);
    struct oak_type_t val_ty = { .id = coll_ty->id };
    if (coll_ty->kind == OAK_TYPE_KIND_ARRAY &&
        oak_trait_find_by_id(&c->traits, coll_ty->id))
      val_ty.kind = OAK_TYPE_KIND_TRAIT;
    oak_compiler_add_local(c,
                           oak_token_text(v_ident->token),
                           oak_token_size(v_ident->token),
                           c->scope.stack_depth - 1,
                           0,
                           val_ty);
  }
}

void oak_compile_for_in(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node)
{
  const usize child_count = oak_child_count(node);
  if (child_count != 3 && child_count != 4)
  {
    oak_compiler_error_at(c, null, "malformed 'for ... in' statement");
    return;
  }

  struct oak_list_entry_t* pos = node->children.next;
  const struct oak_ast_node_t* first_ident =
      oak_container_of(pos, struct oak_ast_node_t, link);
  pos = pos->next;
  const struct oak_ast_node_t* second_ident = null;
  if (child_count == 4)
  {
    second_ident = oak_container_of(pos, struct oak_ast_node_t, link);
    pos = pos->next;
  }
  const struct oak_ast_node_t* coll_expr =
      oak_container_of(pos, struct oak_ast_node_t, link);
  pos = pos->next;
  const struct oak_ast_node_t* body =
      oak_container_of(pos, struct oak_ast_node_t, link);

  const struct oak_code_loc_t loc =
      oak_compiler_loc_from_token(first_ident->token);

  struct oak_type_t coll_ty;
  oak_infer_type(c, coll_expr, &coll_ty);
  if (!oak_type_is_known(&coll_ty) || coll_ty.kind == OAK_TYPE_KIND_SCALAR)
  {
    oak_compiler_error_at(c,
                          coll_expr->token ? coll_expr->token
                                           : first_ident->token,
                          "'for ... in' requires an array or map, got '%s'",
                          oak_type_full_name(c, coll_ty));
    return;
  }

  /* Look up the receiver's size() binding so we can snapshot length once. */
  const struct oak_method_binding_t* len_m =
      coll_ty.kind == OAK_TYPE_KIND_MAP
          ? oak_find_map_method(c, "size")
          : oak_find_array_method(c, "size");
  if (!len_m)
  {
    oak_compiler_error_at(c,
                          coll_expr->token ? coll_expr->token
                                           : first_ident->token,
                          "internal error: missing 'size' method binding");
    return;
  }

  /* Names of the loop variables. Two-var form binds both; one-var form binds
   * only the value (k for maps, v for arrays). */
  const struct oak_ast_node_t* k_ident = null;
  const struct oak_ast_node_t* v_ident = null;
  if (second_ident)
  {
    k_ident = first_ident;
    v_ident = second_ident;
  }
  else
  {
    if (coll_ty.kind == OAK_TYPE_KIND_MAP)
      k_ident = first_ident; /* iterate keys by default */
    else
      v_ident = first_ident; /* arrays: iterate values */
  }

  const int base_depth = c->scope.stack_depth;

  oak_compiler_begin_scope(c);

  /* slot 0: the collection itself (evaluated exactly once). */
  oak_compiler_compile_node(c, coll_expr);
  const int coll_slot = c->scope.stack_depth - 1;
  oak_compiler_add_local(c, "$coll", 0, coll_slot, 0, coll_ty);

  int idx_slot;
  int limit_slot;
  for_in_init_hidden_state(c, loc, len_m, coll_slot, &idx_slot, &limit_slot);

  struct oak_loop_frame_t loop = {
    .enclosing = c->scope.current_loop,
    .loop_start = c->chunk->count,
    .exit_depth = base_depth,
    .continue_depth = base_depth + 3,
    .break_jumps = null,
    .break_count = 0,
    .break_capacity = 0,
    .continue_jumps = null,
    .continue_count = 0,
    .continue_capacity = 0,
  };
  c->scope.current_loop = &loop;

  /* Loop condition: idx < limit (fused compare+branch). */
  oak_compiler_emit_op(
      c, OAK_OP_GET_LOCAL_GET_LOCAL, loc,
      OAK_ARG_U8((u8)idx_slot), OAK_ARG_U8((u8)limit_slot));
  const usize exit_jump =
      oak_compiler_emit_jump(c, OAK_OP_LESS_JUMP_IF_FALSE, loc);

  /* Per-iteration scope: exposes k, v to the body. */
  oak_compiler_begin_scope(c);

  for_in_bind_loop_idents(
      c, loc, &coll_ty, coll_slot, idx_slot, k_ident, v_ident);

  oak_compiler_compile_block(c, body);

  /* Pop per-iter k, v (compile-time + runtime). */
  oak_compiler_end_scope(c);

  /* `continue` lands here (after k/v are popped). */
  oak_compiler_patch_jumps(c, loop.continue_jumps, loop.continue_count);

  oak_compiler_emit_op(c, OAK_OP_INC_LOCAL, loc, OAK_ARG_U8((u8)idx_slot));
  oak_compiler_emit_loop(c, loop.loop_start, loc);
  oak_compiler_patch_jump(c, exit_jump);

  /* Tear down hidden iterator state ($n, $i, $coll). */
  oak_compiler_end_scope(c);

  /* `break` lands here, after all iterator state is popped. */
  oak_compiler_patch_jumps(c, loop.break_jumps, loop.break_count);

  c->scope.current_loop = loop.enclosing;
  oak_dynarr_free(c->allocator, &loop.break_jumps, &loop.break_count, &loop.break_capacity);
  oak_dynarr_free(c->allocator,
      &loop.continue_jumps, &loop.continue_count, &loop.continue_capacity);
}
