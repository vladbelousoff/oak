#include "oak_compiler.h"

#include "oak_log.h"
#include "oak_mem.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define OAK_MAX_LOCALS 256
/* Max recorded forward jumps (break or continue) per loop. */
#define OAK_MAX_LOOP_BRANCHES 64

#define OAK_LOC_SYNTHETIC ((struct oak_code_loc_t){ .line = 0, .column = 1 })

static struct oak_code_loc_t code_loc_from_token(const struct oak_token_t* t)
{
  return (struct oak_code_loc_t){
    .line = oak_token_line(t),
    .column = oak_token_column(t),
  };
}

struct oak_local_t
{
  const char* name;
  size_t length;
  int slot;
  int is_mutable;
  int depth;
};

struct oak_loop_frame_t
{
  struct oak_loop_frame_t* enclosing;
  size_t loop_start;
  int exit_depth;
  int continue_depth;
  size_t break_jumps[OAK_MAX_LOOP_BRANCHES];
  int break_count;
  size_t continue_jumps[OAK_MAX_LOOP_BRANCHES];
  int continue_count;
};

struct oak_compiler_t
{
  struct oak_chunk_t* chunk;
  struct oak_local_t locals[OAK_MAX_LOCALS];
  int local_count;
  int scope_depth;
  int stack_depth;
  int has_error;
  struct oak_loop_frame_t* current_loop;
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void compiler_error_at(struct oak_compiler_t* c,
                              const struct oak_token_t* token,
                              const char* fmt,
                              ...)
{
  static _Thread_local char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (token)
    oak_log(OAK_LOG_ERR,
            "%d:%d: error: %s",
            oak_token_line(token),
            oak_token_column(token),
            buf);
  else
    oak_log(OAK_LOG_ERR, "error: %s", buf);
  c->has_error = 1;
}

static void emit_byte(const struct oak_compiler_t* c,
                      const uint8_t byte,
                      const struct oak_code_loc_t loc)
{
  oak_chunk_write(c->chunk, byte, loc);
}

static void emit_op(struct oak_compiler_t* c,
                    const uint8_t op,
                    const struct oak_code_loc_t loc)
{
  emit_byte(c, op, loc);
  const struct oak_op_info_t* info = oak_op_get_info(op);
  if (info)
    c->stack_depth += info->stack_effect;
}

static void emit_op_arg(struct oak_compiler_t* c,
                        const uint8_t op,
                        const uint8_t arg,
                        const struct oak_code_loc_t loc)
{
  emit_byte(c, op, loc);
  emit_byte(c, arg, loc);
  const struct oak_op_info_t* info = oak_op_get_info(op);
  if (info)
    c->stack_depth += info->stack_effect;
}

static uint8_t intern_constant(struct oak_compiler_t* c,
                               const struct oak_value_t value)
{
  if (c->chunk->const_count >= 256)
  {
    compiler_error_at(c, NULL, "too many constants in one chunk (max 256)");
    return 0;
  }
  const size_t idx = oak_chunk_add_constant(c->chunk, value);
  oak_assert(idx <= 255);
  return (uint8_t)idx;
}

static size_t emit_jump(struct oak_compiler_t* c,
                        const uint8_t op,
                        const struct oak_code_loc_t loc)
{
  emit_op(c, op, loc);
  emit_byte(c, 0xff, loc);
  emit_byte(c, 0xff, loc);
  return c->chunk->count - 2;
}

/* On range error, leaves placeholder operands; do not execute bytecode if
 * has_error. */
static void patch_jump(struct oak_compiler_t* c, const size_t offset)
{
  const size_t jump = c->chunk->count - offset - 2;
  if (jump > 0xffff)
  {
    compiler_error_at(c, NULL, "jump offset too large (max 65535 bytes)");
    return;
  }

  c->chunk->bytecode[offset + 0] = (uint8_t)(jump >> 8 & 0xff);
  c->chunk->bytecode[offset + 1] = (uint8_t)(jump >> 0 & 0xff);
}

static void
patch_jumps(struct oak_compiler_t* c, const size_t* jumps, const int count)
{
  for (int i = 0; i < count; ++i)
    patch_jump(c, jumps[i]);
}

static void emit_loop(struct oak_compiler_t* c,
                      const size_t loop_start,
                      const struct oak_code_loc_t loc)
{
  emit_op(c, OAK_OP_LOOP, loc);
  const size_t jump = c->chunk->count - loop_start + 2;
  if (jump > 0xffff)
  {
    compiler_error_at(c, NULL, "loop body too large (max 65535 bytes)");
    return;
  }

  emit_byte(c, (uint8_t)(jump >> 8 & 0xff), loc);
  emit_byte(c, (uint8_t)(jump >> 0 & 0xff), loc);
}

static void
emit_pops(struct oak_compiler_t* c, int count, const struct oak_code_loc_t loc)
{
  while (count-- > 0)
    emit_op(c, OAK_OP_POP, loc);
}

static void emit_loop_control_jump(struct oak_compiler_t* c,
                                   size_t* jumps,
                                   int* count,
                                   const int target_depth,
                                   const char* keyword)
{
  const int saved_depth = c->stack_depth;
  emit_pops(c, c->stack_depth - target_depth, OAK_LOC_SYNTHETIC);

  if (*count >= OAK_MAX_LOOP_BRANCHES)
  {
    compiler_error_at(c,
                      NULL,
                      "too many '%s' statements in loop (max %d)",
                      keyword,
                      OAK_MAX_LOOP_BRANCHES);
    c->stack_depth = saved_depth;
    return;
  }
  jumps[(*count)++] = emit_jump(c, OAK_OP_JUMP, OAK_LOC_SYNTHETIC);
  c->stack_depth = saved_depth;
}

static int find_local(const struct oak_compiler_t* c,
                      const char* name,
                      const size_t length,
                      int* out_is_mutable)
{
  for (int i = c->local_count - 1; i >= 0; --i)
  {
    const struct oak_local_t* local = &c->locals[i];
    if (local->length == length && memcmp(local->name, name, length) == 0)
    {
      if (out_is_mutable)
        *out_is_mutable = local->is_mutable;
      return local->slot;
    }
  }

  return -1;
}

static void add_local(struct oak_compiler_t* c,
                      const char* name,
                      const size_t length,
                      const int slot,
                      const int is_mutable)
{
  if (c->local_count >= OAK_MAX_LOCALS)
  {
    compiler_error_at(
        c, NULL, "too many local variables (max %d)", OAK_MAX_LOCALS);
    return;
  }
  struct oak_local_t* local = &c->locals[c->local_count++];
  local->name = name;
  local->length = length;
  local->slot = slot;
  local->is_mutable = is_mutable;
  local->depth = c->scope_depth;

  oak_chunk_add_debug_local(c->chunk, slot, name, length);
}

static void begin_scope(struct oak_compiler_t* c)
{
  c->scope_depth++;
}

static void end_scope(struct oak_compiler_t* c)
{
  while (c->local_count > 0 &&
         c->locals[c->local_count - 1].depth == c->scope_depth)
  {
    emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
    c->local_count--;
  }
  c->scope_depth--;
}

static int compile_assign_target(struct oak_compiler_t* c,
                                 const struct oak_ast_node_t* lhs,
                                 const char* non_ident_msg)
{
  if (lhs->kind != OAK_NODE_KIND_IDENT)
  {
    compiler_error_at(c, lhs->token, "%s", non_ident_msg);
    return -1;
  }
  const char* name = oak_token_buf(lhs->token);
  const int name_len = oak_token_size(lhs->token);
  int is_mutable = 0;
  const int slot = find_local(c, name, (size_t)name_len, &is_mutable);
  if (slot < 0)
  {
    compiler_error_at(
        c, lhs->token, "undefined variable '%.*s'", name_len, name);
    return -1;
  }
  if (!is_mutable)
  {
    compiler_error_at(c,
                      lhs->token,
                      "cannot assign to immutable variable '%.*s'",
                      name_len,
                      name);
    return -1;
  }
  return slot;
}

static void compile_node(struct oak_compiler_t* c,
                         const struct oak_ast_node_t* node);

static void compile_block(struct oak_compiler_t* c,
                          const struct oak_ast_node_t* block)
{
  begin_scope(c);
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &block->children)
  {
    compile_node(c, oak_container_of(pos, struct oak_ast_node_t, link));
  }
  end_scope(c);
}

static size_t ast_child_count(const struct oak_ast_node_t* node)
{
  if (oak_node_grammar_op_unary(node->kind))
    return node->child ? 1u : 0u;
  if (oak_node_grammar_op_binary(node->kind))
    return (size_t)(node->lhs ? 1 : 0) + (size_t)(node->rhs ? 1 : 0);
  return oak_list_length(&node->children);
}

static uint8_t opcode_for_node_kind(const enum oak_node_kind_t kind)
{
  switch (kind)
  {
    case OAK_NODE_KIND_BINARY_ADD:
    case OAK_NODE_KIND_STMT_ADD_ASSIGN:
      return OAK_OP_ADD;
    case OAK_NODE_KIND_BINARY_SUB:
    case OAK_NODE_KIND_STMT_SUB_ASSIGN:
      return OAK_OP_SUB;
    case OAK_NODE_KIND_BINARY_MUL:
    case OAK_NODE_KIND_STMT_MUL_ASSIGN:
      return OAK_OP_MUL;
    case OAK_NODE_KIND_BINARY_DIV:
    case OAK_NODE_KIND_STMT_DIV_ASSIGN:
      return OAK_OP_DIV;
    case OAK_NODE_KIND_BINARY_MOD:
    case OAK_NODE_KIND_STMT_MOD_ASSIGN:
      return OAK_OP_MOD;
    case OAK_NODE_KIND_BINARY_EQ:
      return OAK_OP_EQ;
    case OAK_NODE_KIND_BINARY_NEQ:
      return OAK_OP_NEQ;
    case OAK_NODE_KIND_BINARY_LESS:
      return OAK_OP_LT;
    case OAK_NODE_KIND_BINARY_LESS_EQ:
      return OAK_OP_LE;
    case OAK_NODE_KIND_BINARY_GREATER:
      return OAK_OP_GT;
    case OAK_NODE_KIND_BINARY_GREATER_EQ:
      return OAK_OP_GE;
    case OAK_NODE_KIND_UNARY_NEG:
      return OAK_OP_NEGATE;
    case OAK_NODE_KIND_UNARY_NOT:
      return OAK_OP_NOT;
    default:
      oak_assert(0);
      return 0;
  }
}

#define OAK_BUILTIN_PRINT_NAME "print"

static int token_is_print_builtin(const struct oak_token_t* token)
{
  const size_t len = sizeof(OAK_BUILTIN_PRINT_NAME) - 1;
  return oak_token_size(token) == (int)len &&
         memcmp(oak_token_buf(token), OAK_BUILTIN_PRINT_NAME, len) == 0;
}

static void compile_stmt_if(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* node)
{
  oak_assert(ast_child_count(node) >= 2u);

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
          : NULL;

  compile_node(c, cond);
  const size_t then_jump =
      emit_jump(c, OAK_OP_JUMP_IF_FALSE, OAK_LOC_SYNTHETIC);

  compile_block(c, body);

  if (else_node)
  {
    const size_t else_jump = emit_jump(c, OAK_OP_JUMP, OAK_LOC_SYNTHETIC);
    patch_jump(c, then_jump);
    compile_block(c, else_node->child);
    patch_jump(c, else_jump);
  }
  else
  {
    patch_jump(c, then_jump);
  }
}

static void compile_stmt_while(struct oak_compiler_t* c,
                               const struct oak_ast_node_t* node)
{
  if (!node->lhs || !node->rhs)
  {
    compiler_error_at(c, NULL, "malformed 'while' statement");
    return;
  }

  struct oak_loop_frame_t loop = {
    .enclosing = c->current_loop,
    .loop_start = c->chunk->count,
    .exit_depth = c->stack_depth,
    .continue_depth = c->stack_depth,
    .break_count = 0,
    .continue_count = 0,
  };

  /* current_loop points at stack-allocated frame; reset before return. */
  c->current_loop = &loop;

  compile_node(c, node->lhs);
  const size_t exit_jump =
      emit_jump(c, OAK_OP_JUMP_IF_FALSE, OAK_LOC_SYNTHETIC);

  compile_block(c, node->rhs);

  patch_jumps(c, loop.continue_jumps, loop.continue_count);
  emit_loop(c, loop.loop_start, OAK_LOC_SYNTHETIC);
  patch_jump(c, exit_jump);
  patch_jumps(c, loop.break_jumps, loop.break_count);

  c->current_loop = loop.enclosing;
}

static void compile_stmt_for_from(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* node)
{
  oak_assert(ast_child_count(node) >= 4u);

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

  begin_scope(c);

  compile_node(c, from_expr);
  const int loop_var_slot = c->stack_depth - 1;
  add_local(c,
            oak_token_buf(ident->token),
            oak_token_size(ident->token),
            loop_var_slot,
            1);

  compile_node(c, to_expr);
  const int limit_slot = c->stack_depth - 1;
  add_local(c, "", 0, limit_slot, 0);

  struct oak_loop_frame_t loop = {
    .enclosing = c->current_loop,
    .loop_start = c->chunk->count,
    .exit_depth = c->stack_depth - 2,
    .continue_depth = c->stack_depth,
    .break_count = 0,
    .continue_count = 0,
  };

  c->current_loop = &loop;

  {
    const struct oak_code_loc_t ident_loc = code_loc_from_token(ident->token);
    emit_op_arg(c, OAK_OP_GET_LOCAL, (uint8_t)loop_var_slot, ident_loc);
    emit_op_arg(c, OAK_OP_GET_LOCAL, (uint8_t)limit_slot, ident_loc);
    emit_op(c, OAK_OP_LT, ident_loc);
    const size_t exit_jump = emit_jump(c, OAK_OP_JUMP_IF_FALSE, ident_loc);

    compile_block(c, body);

    patch_jumps(c, loop.continue_jumps, loop.continue_count);

    emit_op_arg(c, OAK_OP_GET_LOCAL, (uint8_t)loop_var_slot, ident_loc);
    const uint8_t one_idx = intern_constant(c, OAK_VALUE_I32(1));
    emit_op_arg(c, OAK_OP_CONSTANT, one_idx, ident_loc);
    emit_op(c, OAK_OP_ADD, ident_loc);
    emit_op_arg(c, OAK_OP_SET_LOCAL, (uint8_t)loop_var_slot, ident_loc);
    emit_pops(c, 1, ident_loc);

    emit_loop(c, loop.loop_start, ident_loc);
    patch_jump(c, exit_jump);
  }

  end_scope(c);

  patch_jumps(c, loop.break_jumps, loop.break_count);

  c->current_loop = loop.enclosing;
}

static void compile_fn_call(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* node)
{
  struct oak_ast_node_t* callee;
  struct oak_ast_node_t* arg;
  oak_ast_node_unpack(node, &callee, &arg);

  if (!callee || callee->kind != OAK_NODE_KIND_IDENT)
  {
    compiler_error_at(
        c, callee ? callee->token : NULL, "callee must be an identifier");
    return;
  }

  if (token_is_print_builtin(callee->token))
  {
    const size_t argc = ast_child_count(node) - 1;
    if (argc != 1)
    {
      compiler_error_at(
          c, callee->token, "print() expects 1 argument, got %zu", argc);
      return;
    }
    if (arg->kind == OAK_NODE_KIND_FN_CALL_ARG)
      compile_node(c, arg->child);
    else
      compile_node(c, arg);

    emit_op(c, OAK_OP_PRINT, code_loc_from_token(callee->token));
    return;
  }

  compiler_error_at(c,
                    callee->token,
                    "undefined function '%.*s'",
                    oak_token_size(callee->token),
                    oak_token_buf(callee->token));
}

static void compile_node(struct oak_compiler_t* c,
                         const struct oak_ast_node_t* node)
{
  if (!node || c->has_error)
    return;

  switch (node->kind)
  {
    case OAK_NODE_KIND_PROGRAM:
    {
      struct oak_list_entry_t* pos;
      oak_list_for_each(pos, &node->children)
      {
        const struct oak_ast_node_t* child =
            oak_container_of(pos, struct oak_ast_node_t, link);
        compile_node(c, child);
      }
      emit_op(c, OAK_OP_HALT, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_KIND_INT:
    {
      const int value = oak_token_as_i32(node->token);
      const uint8_t idx = intern_constant(c, OAK_VALUE_I32(value));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_KIND_FLOAT:
    {
      const float value = oak_token_as_f32(node->token);
      const uint8_t idx = intern_constant(c, OAK_VALUE_F32(value));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_KIND_STRING:
    {
      if (c->chunk->const_count >= 256)
      {
        compiler_error_at(c, NULL, "too many constants in one chunk (max 256)");
        return;
      }
      const char* chars = oak_token_buf(node->token);
      const int len = oak_token_size(node->token);
      struct oak_obj_string_t* str = oak_make_string(chars, (size_t)len);
      const uint8_t idx = intern_constant(c, OAK_VALUE_OBJ(str));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_KIND_IDENT:
    {
      const char* name = oak_token_buf(node->token);
      const int len = oak_token_size(node->token);
      const int slot = find_local(c, name, (size_t)len, NULL);
      if (slot < 0)
      {
        compiler_error_at(
            c, node->token, "undefined variable '%.*s'", (int)len, name);
        return;
      }
      emit_op_arg(
          c, OAK_OP_GET_LOCAL, (uint8_t)slot, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_KIND_BINARY_ADD:
    case OAK_NODE_KIND_BINARY_SUB:
    case OAK_NODE_KIND_BINARY_MUL:
    case OAK_NODE_KIND_BINARY_DIV:
    case OAK_NODE_KIND_BINARY_MOD:
    case OAK_NODE_KIND_BINARY_EQ:
    case OAK_NODE_KIND_BINARY_NEQ:
    case OAK_NODE_KIND_BINARY_LESS:
    case OAK_NODE_KIND_BINARY_LESS_EQ:
    case OAK_NODE_KIND_BINARY_GREATER:
    case OAK_NODE_KIND_BINARY_GREATER_EQ:
    case OAK_NODE_KIND_BINARY_AND:
    case OAK_NODE_KIND_BINARY_OR:
    {
      if (node->kind == OAK_NODE_KIND_BINARY_AND ||
          node->kind == OAK_NODE_KIND_BINARY_OR)
      {
        // TODO: short-circuit evaluation; for now, fall through to truthiness
        compiler_error_at(c,
                          NULL,
                          "'%s' operator not yet implemented",
                          node->kind == OAK_NODE_KIND_BINARY_AND ? "&&" : "||");
        return;
      }

      compile_node(c, node->lhs);
      compile_node(c, node->rhs);
      emit_op(c,
              opcode_for_node_kind(node->kind),
              code_loc_from_token(node->lhs->token));
      break;
    }
    case OAK_NODE_KIND_UNARY_NEG:
    case OAK_NODE_KIND_UNARY_NOT:
    {
      compile_node(c, node->child);
      emit_op(c,
              opcode_for_node_kind(node->kind),
              code_loc_from_token(node->child->token));
      break;
    }
    case OAK_NODE_KIND_STMT_EXPR:
    {
      const int depth_before = c->stack_depth;
      struct oak_ast_node_t* expr;
      oak_ast_node_unpack(node, &expr);
      compile_node(c, expr);
      if (c->stack_depth > depth_before)
        emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_KIND_STMT_LET_ASSIGNMENT:
    {
      int is_mutable = 0;
      const struct oak_ast_node_t* assign = NULL;

      struct oak_list_entry_t* pos;
      oak_list_for_each(pos, &node->children)
      {
        const struct oak_ast_node_t* child =
            oak_container_of(pos, struct oak_ast_node_t, link);
        if (child->kind == OAK_NODE_KIND_MUT_KEYWORD)
          is_mutable = 1;
        else if (child->kind == OAK_NODE_KIND_STMT_ASSIGNMENT)
          assign = child;
      }

      if (!assign)
      {
        compiler_error_at(c, NULL, "malformed 'let' statement");
        return;
      }

      const struct oak_ast_node_t* ident = assign->lhs;
      const struct oak_ast_node_t* rhs = assign->rhs;

      compile_node(c, rhs);
      const char* name = oak_token_buf(ident->token);
      const int size = oak_token_size(ident->token);
      add_local(c, name, size, c->stack_depth - 1, is_mutable);

      break;
    }
    case OAK_NODE_KIND_STMT_ASSIGNMENT:
    {
      const struct oak_ast_node_t* lhs = node->lhs;
      const struct oak_ast_node_t* rhs = node->rhs;

      const int slot =
          compile_assign_target(c, lhs, "assignment target must be a variable");
      if (slot < 0)
        return;

      compile_node(c, rhs);
      emit_op_arg(
          c, OAK_OP_SET_LOCAL, (uint8_t)slot, code_loc_from_token(lhs->token));
      emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_KIND_STMT_ADD_ASSIGN:
    case OAK_NODE_KIND_STMT_SUB_ASSIGN:
    case OAK_NODE_KIND_STMT_MUL_ASSIGN:
    case OAK_NODE_KIND_STMT_DIV_ASSIGN:
    case OAK_NODE_KIND_STMT_MOD_ASSIGN:
    {
      const struct oak_ast_node_t* lhs = node->lhs;
      const int slot = compile_assign_target(
          c, lhs, "compound assignment target must be a variable");
      if (slot < 0)
        return;

      emit_op_arg(
          c, OAK_OP_GET_LOCAL, (uint8_t)slot, code_loc_from_token(lhs->token));
      compile_node(c, node->rhs);
      emit_op(
          c, opcode_for_node_kind(node->kind), code_loc_from_token(lhs->token));
      emit_op_arg(
          c, OAK_OP_SET_LOCAL, (uint8_t)slot, code_loc_from_token(lhs->token));
      emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_KIND_FN_CALL:
      compile_fn_call(c, node);
      break;
    case OAK_NODE_KIND_STMT_IF:
      compile_stmt_if(c, node);
      break;
    case OAK_NODE_KIND_STMT_WHILE:
      compile_stmt_while(c, node);
      break;
    case OAK_NODE_KIND_STMT_FOR_FROM:
      compile_stmt_for_from(c, node);
      break;
    case OAK_NODE_KIND_STMT_BREAK:
    case OAK_NODE_KIND_STMT_CONTINUE:
    {
      const int is_break = node->kind == OAK_NODE_KIND_STMT_BREAK;
      const char* keyword = is_break ? "break" : "continue";
      if (!c->current_loop)
      {
        compiler_error_at(c, NULL, "'%s' used outside of a loop", keyword);
        return;
      }
      struct oak_loop_frame_t* loop = c->current_loop;
      emit_loop_control_jump(
          c,
          is_break ? loop->break_jumps : loop->continue_jumps,
          is_break ? &loop->break_count : &loop->continue_count,
          is_break ? loop->exit_depth : loop->continue_depth,
          keyword);
      break;
    }
    default:
      compiler_error_at(c, NULL, "unsupported AST node kind (%d)", node->kind);
      break;
  }
}

struct oak_chunk_t* oak_compile(const struct oak_ast_node_t* root)
{
  struct oak_chunk_t* chunk =
      oak_alloc(sizeof(struct oak_chunk_t), OAK_SRC_LOC);
  oak_chunk_init(chunk);

  struct oak_compiler_t compiler = {
    .chunk = chunk,
    .local_count = 0,
    .scope_depth = 0,
    .stack_depth = 0,
    .has_error = 0,
    .current_loop = NULL,
  };

  compile_node(&compiler, root);

  if (compiler.has_error)
  {
    oak_chunk_free(chunk);
    return NULL;
  }

  return chunk;
}
