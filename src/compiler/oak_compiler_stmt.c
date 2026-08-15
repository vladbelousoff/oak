#include "internal/oak_compiler.h"

/* Compile a condition and emit a branch that pops it and jumps when false.
 * When the condition is a numeric comparison (`<`, `<=`, `>`, `>=`) the
 * comparison and the branch fuse into a single OP_*_JUMP_IF_FALSE, saving a
 * bool push/pop and a dispatch every time the guard runs — the hot path for
 * `if`/`while` conditions and recursion base cases. Returns the patch offset
 * of the 16-bit forward operand, exactly like oak_compiler_emit_jump. */
static usize emit_cond_jump_if_false(oak_compiler_t* c,
                                     const oak_ast_node_t* cond)
{
  oak_compiler_compile_node(c, cond);
  if (c->has_error)
    return oak_chunk_size(c->chunk);

  u8 fused;
  switch (cond->kind)
  {
    case OAK_NODE_BINARY_LESS:
      fused = OAK_OP_LESS_JUMP_IF_FALSE;
      break;
    case OAK_NODE_BINARY_LESS_EQ:
      fused = OAK_OP_LESS_EQUAL_JUMP_IF_FALSE;
      break;
    case OAK_NODE_BINARY_GREATER:
      fused = OAK_OP_GREATER_JUMP_IF_FALSE;
      break;
    case OAK_NODE_BINARY_GREATER_EQ:
      fused = OAK_OP_GREATER_EQUAL_JUMP_IF_FALSE;
      break;
    default:
      return oak_compiler_emit_jump(c, OAK_OP_JUMP_IF_FALSE, OAK_LOC_SYNTHETIC);
  }

  /* A comparison compiles to a single trailing opcode byte. Rewrite it into the
   * fused compare+branch and append the offset placeholder. Guard the rewrite
   * so anything that ever breaks that assumption falls back to a plain branch
   * rather than corrupting the stream. */
  const u8 cmp_op = oak_binop_for_node(cond->kind);
  if (oak_chunk_size(c->chunk) == 0)
    return oak_compiler_emit_jump(c, OAK_OP_JUMP_IF_FALSE, OAK_LOC_SYNTHETIC);

  u8* const code = OAK_DATA(u8, c->chunk->code);
  const usize last = oak_chunk_size(c->chunk) - 1;
  if (code[last] != cmp_op)
    return oak_compiler_emit_jump(c, OAK_OP_JUMP_IF_FALSE, OAK_LOC_SYNTHETIC);

  code[last] = fused;
  oak_compiler_emit_byte(c, 0xff, OAK_LOC_SYNTHETIC);
  oak_compiler_emit_byte(c, 0xff, OAK_LOC_SYNTHETIC);
  /* The comparison's stack effect was already applied when it compiled; account
   * for the extra operand the fused op pops (its effect is one lower). */
  c->scope.stack_depth +=
      oak_op_info[fused].stack_effect - oak_op_info[cmp_op].stack_effect;
  return oak_chunk_size(c->chunk) - 2;
}

void oak_compiler_compile_block(oak_compiler_t* c,
                                const oak_ast_node_t* block)
{
  oak_compiler_begin_scope(c);
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &block->children)
  {
    const int saved_stack = c->scope.stack_depth;
    const int saved_locals = c->scope.local_count;
    oak_compiler_compile_node(
        c, OAK_CONTAINER_OF(pos, oak_ast_node_t, link));
    /* On statement-level error, record it and continue with the next statement
     * so the compiler can report as many independent errors as possible. */
    if (c->has_error)
    {
      c->has_error = 0;
      /* Restore the stack/local state so the next statement compiles cleanly.
       */
      c->scope.stack_depth = saved_stack;
      c->scope.local_count = saved_locals;
    }
  }
  oak_compiler_end_scope(c);
}

void oak_compiler_compile_stmt_if(oak_compiler_t* c,
                                  const oak_ast_node_t* node)
{
  OAK_ASSERT(oak_child_count(node) >= 2u);

  oak_list_entry_t* pos = node->children.next;
  const oak_ast_node_t* cond =
      OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
  pos = pos->next;
  const oak_ast_node_t* body =
      OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
  pos = pos->next;
  const oak_ast_node_t* else_node =
      (pos != &node->children)
          ? OAK_CONTAINER_OF(pos, oak_ast_node_t, link)
          : OAK_NULL;

  oak_reject_void(c, cond);
  if (c->has_error)
    return;

  const usize then_jump = emit_cond_jump_if_false(c, cond);

  /* After the condition + JUMP_IF_FALSE, only parameter/locals slots remain on
   * the stack model. The then/else branches must not leave extra stack slots
   * in the compiler's depth tracking (e.g. void `return` pushes a dummy 0),
   * or subsequent statements assign wrong local slots. */
  const int merge_stack_depth = c->scope.stack_depth;

  oak_compiler_compile_block(c, body);
  c->scope.stack_depth = merge_stack_depth;

  if (else_node)
  {
    const usize else_jump =
        oak_compiler_emit_jump(c, OAK_OP_JUMP, OAK_LOC_SYNTHETIC);
    oak_compiler_patch_jump(c, then_jump);
    oak_compiler_compile_block(c, else_node->child);
    c->scope.stack_depth = merge_stack_depth;
    oak_compiler_patch_jump(c, else_jump);
  }
  else
  {
    oak_compiler_patch_jump(c, then_jump);
  }
}

void oak_compile_while(oak_compiler_t* c,
                                     const oak_ast_node_t* node)
{
  if (!node->lhs || !node->rhs)
  {
    oak_compiler_error_at(c, OAK_NULL, "malformed 'while' statement");
    return;
  }

  oak_loop_frame_t loop = {
    .enclosing = c->scope.current_loop,
    .loop_start = oak_chunk_size(c->chunk),
    .exit_depth = c->scope.stack_depth,
    .continue_depth = c->scope.stack_depth,
    .break_jumps = OAK_NULL,
    .continue_jumps = OAK_NULL,
  };
  loop.break_jumps = oak_vector_new(c->allocator, sizeof(usize));
  loop.continue_jumps = oak_vector_new(c->allocator, sizeof(usize));
  OAK_ASSERT(loop.break_jumps && loop.continue_jumps);

  /* current_loop points at a stack-allocated frame; reset before return. */
  c->scope.current_loop = &loop;

  oak_reject_void(c, node->lhs);
  if (c->has_error)
  {
    c->scope.current_loop = loop.enclosing;
    oak_destroy(loop.break_jumps);
    oak_destroy(loop.continue_jumps);
    return;
  }

  const usize exit_jump = emit_cond_jump_if_false(c, node->lhs);

  oak_compiler_compile_block(c, node->rhs);

  oak_compiler_patch_jumps(c, loop.continue_jumps);
  oak_compiler_emit_loop(c, loop.loop_start, OAK_LOC_SYNTHETIC);
  oak_compiler_patch_jump(c, exit_jump);
  oak_compiler_patch_jumps(c, loop.break_jumps);

  c->scope.current_loop = loop.enclosing;
  oak_destroy(loop.break_jumps);
  oak_destroy(loop.continue_jumps);
}
