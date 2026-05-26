#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

usize oakc_child_count(const struct oak_ast_node_t* node)
{
  if (oak_node_is_unary_op(node->kind))
    return node->child ? 1u : 0u;
  if (oak_node_is_binary_op(node->kind))
    return (usize)(node->lhs ? 1 : 0) + (usize)(node->rhs ? 1 : 0);
  return oak_list_length(&node->children);
}

int oakc_is_int_literal(const struct oak_ast_node_t* node,
                                    const int value)
{
  return node && node->kind == OAK_NODE_INT &&
         oak_token_as_i32(node->token) == value;
}

u8 oakc_binop_for_node(const enum oak_node_kind_t kind)
{
  switch (kind)
  {
    case OAK_NODE_BINARY_ADD:
    case OAK_NODE_STMT_ADD_ASSIGN:
      return OAK_OP_ADD;
    case OAK_NODE_BINARY_SUB:
    case OAK_NODE_STMT_SUB_ASSIGN:
      return OAK_OP_SUBTRACT;
    case OAK_NODE_BINARY_MUL:
    case OAK_NODE_STMT_MUL_ASSIGN:
      return OAK_OP_MULTIPLY;
    case OAK_NODE_BINARY_DIV:
    case OAK_NODE_STMT_DIV_ASSIGN:
      return OAK_OP_DIVIDE;
    case OAK_NODE_BINARY_INT_DIV:
      return OAK_OP_INT_DIVIDE;
    case OAK_NODE_BINARY_MOD:
    case OAK_NODE_STMT_MOD_ASSIGN:
      return OAK_OP_MODULO;
    case OAK_NODE_BINARY_EQ:
      return OAK_OP_EQUAL;
    case OAK_NODE_BINARY_NEQ:
      return OAK_OP_NOT_EQUAL;
    case OAK_NODE_BINARY_LESS:
      return OAK_OP_LESS;
    case OAK_NODE_BINARY_LESS_EQ:
      return OAK_OP_LESS_EQUAL;
    case OAK_NODE_BINARY_GREATER:
      return OAK_OP_GREATER;
    case OAK_NODE_BINARY_GREATER_EQ:
      return OAK_OP_GREATER_EQUAL;
    default:
      oak_assert(0);
      return 0;
  }
}

u8 oakc_op_for_node(const enum oak_node_kind_t kind)
{
  switch (kind)
  {
    case OAK_NODE_UNARY_NEG:
      return OAK_OP_NEGATE;
    case OAK_NODE_UNARY_NOT:
      return OAK_OP_NOT;
    default:
      oak_assert(0);
      return 0;
  }
}

void oak_compiler_compile_node(struct oak_compiler_t* c,
                               const struct oak_ast_node_t* node)
{
  if (!node || c->has_error)
    return;

  switch (node->kind)
  {
    case OAK_NODE_PROGRAM:
      oak_compiler_error_at(
          c, null, "internal error: nested program in compilation");
      break;
    case OAK_NODE_FN_DECL:
      break;
    case OAK_NODE_STMT_RETURN:
      oakc_compile_return(c, node);
      break;
    case OAK_NODE_INT:
    {
      const int value = oak_token_as_i32(node->token);
      const struct oak_code_loc_t loc =
          oak_compiler_loc_from_token(node->token);
      if (value >= -128 && value <= 127)
      {
        oak_compiler_emit_op(
            c, OAK_OP_PUSH_INT8, loc, OAK_ARG_U8((u8)(value & 0xFF)));
      }
      else
      {
        const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_I32(value));
        oak_compiler_emit_constant(c, idx, loc);
      }
      break;
    }
    case OAK_NODE_FLOAT:
    {
      const float value = oak_token_as_f32(node->token);
      const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_F32(value));
      oak_compiler_emit_constant(
          c, idx, oak_compiler_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_STRING:
    {
      const char* chars = oak_token_text(node->token);
      const int len = oak_token_size(node->token);
      struct oak_obj_string_t* str = oak_string_new(c->allocator, chars, len);
      const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(str));
      oak_compiler_emit_constant(
          c, idx, oak_compiler_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_TRUE:
      oak_compiler_emit_op(
          c, OAK_OP_TRUE, oak_compiler_loc_from_token(node->token));
      break;
    case OAK_NODE_FALSE:
      oak_compiler_emit_op(
          c, OAK_OP_FALSE, oak_compiler_loc_from_token(node->token));
      break;
    case OAK_NODE_NONE_LITERAL:
      oak_compiler_emit_op(
          c, OAK_OP_NONE, oak_compiler_loc_from_token(node->token));
      break;
    case OAK_NODE_IDENT:
    {
      const char* name = oak_token_text(node->token);
      const int len = oak_token_size(node->token);
      const int slot = oak_compiler_find_local(c, name, null);
      if (slot >= 0)
      {
        oak_compiler_emit_op(c,
                             OAK_OP_GET_LOCAL,
                             oak_compiler_loc_from_token(node->token),
                             OAK_ARG_U8((u8)slot));
        break;
      }
      if (c->scope.fn_depth > 0 &&
          oakc_is_module_scope(c, name, len))
      {
        oak_compiler_error_at(c,
                              node->token,
                              "'%s' is not visible here (module scope only)",
                              name);
        return;
      }
      oak_compiler_error_at(c, node->token, "undefined variable '%s'", name);
      return;
    }
    case OAK_NODE_SELF:
    {
      const int slot = oak_compiler_find_local(c, "self", null);
      if (slot < 0)
      {
        oak_compiler_error_at(
            c, node->token, "'self' is only valid inside a method body");
        return;
      }
      oak_compiler_emit_op(c,
                           OAK_OP_GET_LOCAL,
                           oak_compiler_loc_from_token(node->token),
                           OAK_ARG_U8((u8)slot));
      break;
    }
    case OAK_NODE_BINARY_ADD:
    case OAK_NODE_BINARY_SUB:
    case OAK_NODE_BINARY_MUL:
    case OAK_NODE_BINARY_DIV:
    case OAK_NODE_BINARY_INT_DIV:
    case OAK_NODE_BINARY_MOD:
    case OAK_NODE_BINARY_EQ:
    case OAK_NODE_BINARY_NEQ:
    case OAK_NODE_BINARY_LESS:
    case OAK_NODE_BINARY_LESS_EQ:
    case OAK_NODE_BINARY_GREATER:
    case OAK_NODE_BINARY_GREATER_EQ:
      oak_compiler_compile_binary_op(c, node);
      break;
    case OAK_NODE_BINARY_AND:
      oak_compiler_compile_binary_and(c, node);
      break;
    case OAK_NODE_BINARY_OR:
      oak_compiler_compile_binary_or(c, node);
      break;
    case OAK_NODE_UNARY_NEG:
    case OAK_NODE_UNARY_NOT:
    {
      oakc_reject_void(c, node->child);
      if (c->has_error)
        return;
      oak_compiler_compile_node(c, node->child);
      oak_compiler_emit_op(c,
                           oakc_op_for_node(node->kind),
                           oak_compiler_loc_from_token(node->child->token));
      break;
    }
    case OAK_NODE_STMT_EXPR:
    {
      const int depth_before = c->scope.stack_depth;
      struct oak_ast_node_t* expr = node->child;
      oak_compiler_compile_node(c, expr);
      /* Expression statements must not leave any temporary values on the VM
       * stack. A single OP_POP was insufficient for some call shapes (e.g.
       * nested native + user calls inside a for-body), leaving garbage above
       * locals so OP_RETURN decref'd the wrong slots and crashed. */
      while (c->scope.stack_depth > depth_before)
        oak_compiler_emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_STMT_LET_ASSIGNMENT:
      oak_compiler_compile_let_assignment(c, node);
      break;
    case OAK_NODE_STMT_ASSIGNMENT:
      oak_compiler_compile_stmt_assignment(c, node);
      break;
    case OAK_NODE_STMT_ADD_ASSIGN:
    case OAK_NODE_STMT_SUB_ASSIGN:
    case OAK_NODE_STMT_MUL_ASSIGN:
    case OAK_NODE_STMT_DIV_ASSIGN:
    case OAK_NODE_STMT_MOD_ASSIGN:
      oak_compiler_compile_compound_assign(c, node);
      break;
    case OAK_NODE_FN_CALL:
      oak_compiler_compile_fn_call(c, node);
      break;
    case OAK_NODE_EXPR_EMPTY_ARRAY:
      oak_compiler_error_at(
          c,
          null,
          "untyped array literal; arrays must be typed (e.g. '[] as "
          "number[]')");
      break;
    case OAK_NODE_EXPR_ARRAY_LITERAL:
      oak_compiler_compile_array_literal(c, node);
      break;
    case OAK_NODE_EXPR_EMPTY_MAP:
      oak_compiler_error_at(
          c,
          null,
          "untyped map literal; maps must be typed (e.g. '[:] as "
          "[string:number]')");
      break;
    case OAK_NODE_EXPR_MAP_LITERAL:
      oak_compiler_compile_map_literal(c, node);
      break;
    case OAK_NODE_EXPR_CAST:
      oak_compiler_compile_cast(c, node);
      break;
    case OAK_NODE_INDEX_ACCESS:
    {
      oakc_reject_void(c, node->lhs);
      if (c->has_error)
        return;
      oakc_reject_void(c, node->rhs);
      if (c->has_error)
        return;
      oak_compiler_compile_node(c, node->lhs);
      oak_compiler_compile_node(c, node->rhs);
      oak_compiler_emit_op(c, OAK_OP_GET_INDEX, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_MEMBER_ACCESS:
      oak_compiler_compile_member_access(c, node);
      break;
    case OAK_NODE_EXPR_RECORD_LITERAL:
      oak_compiler_compile_record_literal(c, node);
      break;
    case OAK_NODE_STMT_IF:
      oak_compiler_compile_stmt_if(c, node);
      break;
    case OAK_NODE_STMT_WHILE:
      oakc_compile_while(c, node);
      break;
    case OAK_NODE_STMT_FOR_FROM:
      oakc_compile_for_from(c, node);
      break;
    case OAK_NODE_STMT_FOR_IN:
      oakc_compile_for_in(c, node);
      break;
    case OAK_NODE_STMT_BREAK:
    case OAK_NODE_STMT_CONTINUE:
    {
      const int is_break = node->kind == OAK_NODE_STMT_BREAK;
      const char* keyword = is_break ? "break" : "continue";
      if (!c->scope.current_loop)
      {
        oak_compiler_error_at(c, null, "'%s' used outside of a loop", keyword);
        return;
      }
      struct oak_loop_frame_t* loop = c->scope.current_loop;
      oakc_emit_loop_jump(
          c,
          is_break ? &loop->break_jumps : &loop->continue_jumps,
          is_break ? &loop->break_count : &loop->continue_count,
          is_break ? &loop->break_capacity : &loop->continue_capacity,
          is_break ? loop->exit_depth : loop->continue_depth);
      break;
    }
    default:
      oak_compiler_error_at(
          c, null, "unsupported AST node kind (%d)", node->kind);
      break;
  }
}
