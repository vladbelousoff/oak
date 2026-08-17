#include "internal/oak_compiler.h"

void oak_compile_for_from(oak_compiler_t* c,
                                        const oak_ast_node_t* node)
{
  OAK_ASSERT(oak_child_count(node) >= 4u);

  oak_list_entry_t* pos = node->children.next;
  const oak_ast_node_t* ident =
      OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
  pos = pos->next;
  const oak_ast_node_t* from_expr =
      OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
  pos = pos->next;
  const oak_ast_node_t* to_expr =
      OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
  pos = pos->next;
  const oak_ast_node_t* body =
      OAK_CONTAINER_OF(pos, oak_ast_node_t, link);

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

  oak_type_t from_ty;
  oak_infer_type(c, from_expr, &from_ty);
  if (!oak_type_is_known(&from_ty))
    from_ty.id = OAK_TYPE_NUMBER;

  oak_compiler_compile_node(c, from_expr);
  const int loop_var_slot = c->scope.stack_depth - 1;
  oak_compiler_add_local(c,
                         oak_token_text(ident->token),
                         loop_var_slot,
                         1,
                         from_ty);

  oak_type_t to_ty;
  oak_infer_type(c, to_expr, &to_ty);
  if (!oak_type_is_known(&to_ty))
    to_ty.id = OAK_TYPE_NUMBER;

  oak_compiler_compile_node(c, to_expr);
  const int limit_slot = c->scope.stack_depth - 1;
  oak_compiler_add_local(c, "", limit_slot, 0, to_ty);

  oak_loop_frame_t loop = {
    .enclosing = c->scope.current_loop,
    .loop_start = oak_chunk_size(c->chunk),
    .exit_depth = c->scope.stack_depth - 2,
    .continue_depth = c->scope.stack_depth,
    .break_jumps = OAK_NULL,
    .continue_jumps = OAK_NULL,
  };
  loop.break_jumps = oak_vector_new(c->allocator, sizeof(usize));
  loop.continue_jumps = oak_vector_new(c->allocator, sizeof(usize));
  OAK_ASSERT(loop.break_jumps && loop.continue_jumps);

  c->scope.current_loop = &loop;

  {
    const oak_code_loc_t ident_loc =
        oak_compiler_loc_from_token(ident->token);
    OAK_COMPILER_EMIT_OP(
        c, OAK_OP_GET_LOCAL_GET_LOCAL, ident_loc,
        OAK_ARG_U8((u8)loop_var_slot), OAK_ARG_U8((u8)limit_slot));
    const usize exit_jump =
        oak_compiler_emit_jump(c, OAK_OP_LESS_JUMP_IF_FALSE, ident_loc);

    const int merge_stack_depth = c->scope.stack_depth;

    oak_compiler_compile_block(c, body);
    c->scope.stack_depth = merge_stack_depth;

    oak_compiler_patch_jumps(c, loop.continue_jumps);

    {
      oak_compiler_emit_byte(c, OAK_OP_INC_LOCAL_LOOP, ident_loc);
      oak_compiler_emit_byte(c, (u8)loop_var_slot, ident_loc);
      const usize jump = oak_chunk_size(c->chunk) - loop.loop_start + 2;
      if (jump > 0xFFFFu)
      {
        oak_compiler_error_at(
            c, OAK_NULL,
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

  oak_compiler_patch_jumps(c, loop.break_jumps);

  c->scope.current_loop = loop.enclosing;
  oak_destroy(loop.break_jumps);
  oak_destroy(loop.continue_jumps);
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

static void for_in_init_hidden_state(oak_compiler_t* c,
                                     const oak_code_loc_t loc,
                                     const oak_method_binding_t* len_m,
                                     const int coll_slot,
                                     int* out_idx_slot,
                                     int* out_limit_slot)
{
  oak_compiler_emit_constant(
      c, oak_compiler_intern_constant(c, OAK_VALUE_I32(0)), loc);
  *out_idx_slot = c->scope.stack_depth - 1;
  const oak_type_t num_ty = { .id = OAK_TYPE_NUMBER };
  oak_compiler_add_local(c, "$i", *out_idx_slot, 1, num_ty);

  oak_compiler_emit_constant(c, len_m->const_idx, loc);
  OAK_COMPILER_EMIT_OP(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)coll_slot));
  OAK_COMPILER_EMIT_OP(c, OAK_OP_CALL, loc, OAK_ARG_U8((u8)len_m->total_arity));
  c->scope.stack_depth -= len_m->total_arity;
  *out_limit_slot = c->scope.stack_depth - 1;
  oak_compiler_add_local(c, "$n", *out_limit_slot, 0, num_ty);
}

/* `coll_is_mutable` is the access the collection expression grants. The value
 * binding is a reference into the collection, so it inherits that access --
 * iterating something writable lets the body write through the element, and
 * iterating a read-only collection does not. The key binding stays read-only:
 * a map key is matched by equality, never a place in the collection. */
static void for_in_bind_loop_idents(oak_compiler_t* c,
                                    const oak_code_loc_t loc,
                                    const oak_type_t* coll_ty,
                                    const int coll_slot,
                                    const int idx_slot,
                                    const int coll_is_mutable,
                                    const oak_ast_node_t* k_ident,
                                    const oak_ast_node_t* v_ident)
{
  if (k_ident)
  {
    if (coll_ty->kind == OAK_TYPE_KIND_MAP)
    {
      OAK_COMPILER_EMIT_OP(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)coll_slot));
      OAK_COMPILER_EMIT_OP(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)idx_slot));
      OAK_COMPILER_EMIT_OP(c, OAK_OP_MAP_KEY_AT, loc);
      const oak_type_t key_ty = { .id = coll_ty->key_id };
      oak_compiler_add_local(c,
                             oak_token_text(k_ident->token),
                             c->scope.stack_depth - 1,
                             0,
                             key_ty);
    }
    else
    {
      OAK_COMPILER_EMIT_OP(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)idx_slot));
      const oak_type_t num_ty = { .id = OAK_TYPE_NUMBER };
      oak_compiler_add_local(c,
                             oak_token_text(k_ident->token),
                             c->scope.stack_depth - 1,
                             0,
                             num_ty);
    }
  }
  if (v_ident)
  {
    OAK_COMPILER_EMIT_OP(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)coll_slot));
    OAK_COMPILER_EMIT_OP(c, OAK_OP_GET_LOCAL, loc, OAK_ARG_U8((u8)idx_slot));
    OAK_COMPILER_EMIT_OP(c,
                         coll_ty->kind == OAK_TYPE_KIND_MAP ? OAK_OP_MAP_VAL_AT
                                                            : OAK_OP_GET_INDEX,
                         loc);
    oak_type_t val_ty = { .id = coll_ty->id };
    if (coll_ty->kind == OAK_TYPE_KIND_ARRAY &&
        oak_interface_find_by_id(&c->interfaces, coll_ty->id))
      val_ty.kind = OAK_TYPE_KIND_INTERFACE;
    oak_compiler_add_local(c,
                           oak_token_text(v_ident->token),
                           c->scope.stack_depth - 1,
                           coll_is_mutable,
                           val_ty);
  }
}

void oak_compile_for_in(oak_compiler_t* c,
                                      const oak_ast_node_t* node)
{
  const usize child_count = oak_child_count(node);
  if (child_count != 3 && child_count != 4)
  {
    oak_compiler_error_at(c, OAK_NULL, "malformed 'for ... in' statement");
    return;
  }

  oak_list_entry_t* pos = node->children.next;
  const oak_ast_node_t* first_ident =
      OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
  pos = pos->next;
  const oak_ast_node_t* second_ident = OAK_NULL;
  if (child_count == 4)
  {
    second_ident = OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    pos = pos->next;
  }
  const oak_ast_node_t* coll_expr =
      OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
  pos = pos->next;
  const oak_ast_node_t* body =
      OAK_CONTAINER_OF(pos, oak_ast_node_t, link);

  const oak_code_loc_t loc =
      oak_compiler_loc_from_token(first_ident->token);

  oak_type_t coll_ty;
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

  /* Read while the enclosing scope is still current: `$coll` and the loop
   * idents are about to shadow it. */
  const int coll_is_mutable = oak_compiler_expr_is_mutable_place(c, coll_expr);

  /* Look up the receiver's size() binding so we can snapshot length once. */
  const oak_method_binding_t* len_m =
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
  const oak_ast_node_t* k_ident = OAK_NULL;
  const oak_ast_node_t* v_ident = OAK_NULL;
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
  oak_compiler_add_local(c, "$coll", coll_slot, 0, coll_ty);

  int idx_slot;
  int limit_slot;
  for_in_init_hidden_state(c, loc, len_m, coll_slot, &idx_slot, &limit_slot);

  oak_loop_frame_t loop = {
    .enclosing = c->scope.current_loop,
    .loop_start = oak_chunk_size(c->chunk),
    .exit_depth = base_depth,
    .continue_depth = base_depth + 3,
    .break_jumps = OAK_NULL,
    .continue_jumps = OAK_NULL,
  };
  loop.break_jumps = oak_vector_new(c->allocator, sizeof(usize));
  loop.continue_jumps = oak_vector_new(c->allocator, sizeof(usize));
  OAK_ASSERT(loop.break_jumps && loop.continue_jumps);
  c->scope.current_loop = &loop;

  /* Loop condition: idx < limit (fused compare+branch). */
  OAK_COMPILER_EMIT_OP(
      c, OAK_OP_GET_LOCAL_GET_LOCAL, loc,
      OAK_ARG_U8((u8)idx_slot), OAK_ARG_U8((u8)limit_slot));
  const usize exit_jump =
      oak_compiler_emit_jump(c, OAK_OP_LESS_JUMP_IF_FALSE, loc);

  /* Per-iteration scope: exposes k, v to the body. */
  oak_compiler_begin_scope(c);

  for_in_bind_loop_idents(c,
                          loc,
                          &coll_ty,
                          coll_slot,
                          idx_slot,
                          coll_is_mutable,
                          k_ident,
                          v_ident);

  oak_compiler_compile_block(c, body);

  /* Pop per-iter k, v (compile-time + runtime). */
  oak_compiler_end_scope(c);

  /* `continue` lands here (after k/v are popped). */
  oak_compiler_patch_jumps(c, loop.continue_jumps);

  OAK_COMPILER_EMIT_OP(c, OAK_OP_INC_LOCAL, loc, OAK_ARG_U8((u8)idx_slot));
  oak_compiler_emit_loop(c, loop.loop_start, loc);
  oak_compiler_patch_jump(c, exit_jump);

  /* Tear down hidden iterator state ($n, $i, $coll). */
  oak_compiler_end_scope(c);

  /* `break` lands here, after all iterator state is popped. */
  oak_compiler_patch_jumps(c, loop.break_jumps);

  c->scope.current_loop = loop.enclosing;
  oak_destroy(loop.break_jumps);
  oak_destroy(loop.continue_jumps);
}
