#include "internal/oak_compiler.h"

void oak_compiler_compile_block(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* block)
{
  oak_compiler_begin_scope(c);
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &block->children)
  {
    const int saved_stack = c->scope.stack_depth;
    const int saved_locals = c->scope.local_count;
    oak_compiler_compile_node(
        c, oak_container_of(pos, struct oak_ast_node_t, link));
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

void oak_compiler_compile_stmt_if(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* node)
{
  oak_assert(oc_child_count(node) >= 2u);

  struct oak_list_entry_t* pos = node->children.next;
  const struct oak_ast_node_t* cond =
      oak_container_of(pos, struct oak_ast_node_t, link);
  pos = pos->next;
  const struct oak_ast_node_t* body =
      oak_container_of(pos, struct oak_ast_node_t, link);
  pos = pos->next;
  const struct oak_ast_node_t* else_node =
      (pos != &node->children)
          ? oak_container_of(pos, struct oak_ast_node_t, link)
          : null;

  oc_reject_void(c, cond);
  if (c->has_error)
    return;

  oak_compiler_compile_node(c, cond);
  const usize then_jump =
      oak_compiler_emit_jump(c, OAK_OP_JUMP_IF_FALSE, OAK_LOC_SYNTHETIC);

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

void oc_compile_while(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* node)
{
  if (!node->lhs || !node->rhs)
  {
    oak_compiler_error_at(c, null, "malformed 'while' statement");
    return;
  }

  struct oak_loop_frame_t loop = {
    .enclosing = c->scope.current_loop,
    .loop_start = c->chunk->count,
    .exit_depth = c->scope.stack_depth,
    .continue_depth = c->scope.stack_depth,
    .break_count = 0,
    .continue_count = 0,
  };

  /* current_loop points at a stack-allocated frame; reset before return. */
  c->scope.current_loop = &loop;

  oc_reject_void(c, node->lhs);
  if (c->has_error)
  {
    c->scope.current_loop = loop.enclosing;
    return;
  }

  oak_compiler_compile_node(c, node->lhs);
  const usize exit_jump =
      oak_compiler_emit_jump(c, OAK_OP_JUMP_IF_FALSE, OAK_LOC_SYNTHETIC);

  oak_compiler_compile_block(c, node->rhs);

  oak_compiler_patch_jumps(c, loop.continue_jumps, loop.continue_count);
  oak_compiler_emit_loop(c, loop.loop_start, OAK_LOC_SYNTHETIC);
  oak_compiler_patch_jump(c, exit_jump);
  oak_compiler_patch_jumps(c, loop.break_jumps, loop.break_count);

  c->scope.current_loop = loop.enclosing;
}
