#include "internal/oak_compiler.h"

void oak_compiler_reject_binary_void(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* node)
{
  oakc_reject_void(c, node->lhs);
  if (c->has_error)
    return;
  oakc_reject_void(c, node->rhs);
}

/* Returns 1 if `t` is a registered enum type. */
static int type_is_enum(struct oak_compiler_t* c, const struct oak_type_t* t)
{
  if (!t || t->kind != OAK_TYPE_KIND_SCALAR)
    return 0;
  if (t->id < OAK_TYPE_FIRST_USER || t->id >= c->types.count)
    return 0;
  const char* name = c->types.entries[t->id].name;
  if (!name)
    return 0;
  return oakc_is_enum_name(
      &c->enums, name, c->types.entries[t->id].len);
}

/* Static type check for binary operators applied to enum operands. */
void oak_compiler_reject_binary_enum_misuse(struct oak_compiler_t* c,
                                            const struct oak_ast_node_t* node)
{
  struct oak_type_t lt;
  struct oak_type_t rt;
  oakc_infer_type(c, node->lhs, &lt);
  oakc_infer_type(c, node->rhs, &rt);
  const int le = type_is_enum(c, &lt);
  const int re = type_is_enum(c, &rt);

  const enum oak_node_kind_t k = node->kind;
  const int is_eq = (k == OAK_NODE_BINARY_EQ || k == OAK_NODE_BINARY_NEQ);
  const struct oak_token_t* tok = node->lhs ? node->lhs->token : node->token;

  if (is_eq)
  {
    if (le != re || (le && lt.id != rt.id))
    {
      oak_compiler_error_at(
          c,
          tok,
          "cannot compare '%s' and '%s'; enum values may only be compared "
          "to the same enum type",
          oakc_type_full_name(c, lt),
          oakc_type_full_name(c, rt));
    }
    return;
  }
  if (le || re)
  {
    oak_compiler_error_at(
        c,
        tok,
        "operator not supported on enum values (operands: '%s', '%s')",
        oakc_type_full_name(c, lt),
        oakc_type_full_name(c, rt));
  }
}

void oak_compiler_compile_binary_op(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* node)
{
  oak_compiler_reject_binary_void(c, node);
  if (c->has_error)
    return;
  oak_compiler_reject_binary_enum_misuse(c, node);
  if (c->has_error)
    return;
  oak_compiler_compile_node(c, node->lhs);
  oak_compiler_compile_node(c, node->rhs);
  oak_compiler_emit_op(
      c,
      oakc_binop_for_node(node->kind),
      oak_compiler_loc_from_token(node->lhs->token));
}

void oak_compiler_compile_binary_and(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* node)
{
  oak_compiler_reject_binary_void(c, node);
  if (c->has_error)
    return;
  /* Short-circuit &&:
   *   evaluate lhs
   *   JUMP_IF_FALSE [false_branch]   ; pops lhs; jump if lhs is falsy
   *   evaluate rhs
   *   BOOL                           ; normalise rhs to bool
   *   JUMP [end]
   *   [false_branch]: FALSE
   *   [end]:
   */
  const struct oak_code_loc_t loc =
      node->lhs ? oak_compiler_loc_from_token(node->lhs->token)
                : OAK_LOC_SYNTHETIC;
  oak_compiler_compile_node(c, node->lhs);
  if (c->has_error)
    return;
  const usize false_jump =
      oak_compiler_emit_jump(c, OAK_OP_JUMP_IF_FALSE, loc);
  const int depth_after_jif = c->scope.stack_depth;
  oak_compiler_compile_node(c, node->rhs);
  if (c->has_error)
    return;
  oak_compiler_emit_op(c, OAK_OP_BOOL, loc);
  const usize end_jump = oak_compiler_emit_jump(c, OAK_OP_JUMP, loc);
  oak_compiler_patch_jump(c, false_jump);
  c->scope.stack_depth = depth_after_jif;
  oak_compiler_emit_op(c, OAK_OP_FALSE, loc);
  oak_compiler_patch_jump(c, end_jump);
}

void oak_compiler_compile_binary_or(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* node)
{
  oak_compiler_reject_binary_void(c, node);
  if (c->has_error)
    return;
  /* Short-circuit ||:
   *   evaluate lhs
   *   JUMP_IF_TRUE [true_branch]     ; pops lhs; jump if lhs is truthy
   *   evaluate rhs
   *   BOOL                           ; normalise rhs to bool
   *   JUMP [end]
   *   [true_branch]: TRUE
   *   [end]:
   */
  const struct oak_code_loc_t loc =
      node->lhs ? oak_compiler_loc_from_token(node->lhs->token)
                : OAK_LOC_SYNTHETIC;
  oak_compiler_compile_node(c, node->lhs);
  if (c->has_error)
    return;
  const usize true_jump =
      oak_compiler_emit_jump(c, OAK_OP_JUMP_IF_TRUE, loc);
  const int depth_after_jif = c->scope.stack_depth;
  oak_compiler_compile_node(c, node->rhs);
  if (c->has_error)
    return;
  oak_compiler_emit_op(c, OAK_OP_BOOL, loc);
  const usize end_jump = oak_compiler_emit_jump(c, OAK_OP_JUMP, loc);
  oak_compiler_patch_jump(c, true_jump);
  c->scope.stack_depth = depth_after_jif;
  oak_compiler_emit_op(c, OAK_OP_TRUE, loc);
  oak_compiler_patch_jump(c, end_jump);
}
