#include "oak_compiler.h"

#include "oak_log.h"
#include "oak_mem.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define OAK_MAX_LOCALS 256
#define OAK_MAX_BREAKS 64

typedef struct
{
  const char* name;
  size_t length;
  int slot;
  int mutable;
  int depth;
} oak_local_t;

typedef struct oak_loop_ctx_t
{
  struct oak_loop_ctx_t* enclosing;
  size_t loop_start;
  int exit_depth;
  int continue_depth;
  size_t break_jumps[OAK_MAX_BREAKS];
  int break_count;
  size_t continue_jumps[OAK_MAX_BREAKS];
  int continue_count;
} oak_loop_ctx_t;

typedef struct
{
  oak_chunk_t* chunk;
  oak_local_t locals[OAK_MAX_LOCALS];
  int local_count;
  int scope_depth;
  int stack_depth;
  int had_error;
  oak_loop_ctx_t* current_loop;
} oak_compiler_t;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void
compiler_error_at(oak_compiler_t* c,
                  const oak_token_t* token,
                  const char* fmt,
                  ...)
{
  static _Thread_local char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (token)
    oak_log(OAK_LOG_ERR, "%d:%d: error: %s", oak_token_line(token), oak_token_column(token), buf);
  else
    oak_log(OAK_LOG_ERR, "error: %s", buf);
  c->had_error = 1;
}

static void
emit_byte(const oak_compiler_t* c, const uint8_t byte, const int line)
{
  oak_chunk_write(c->chunk, byte, line);
}

static void emit_op(oak_compiler_t* c, const uint8_t op, const int line)
{
  emit_byte(c, op, line);
  const oak_op_info_t* info = oak_op_get_info(op);
  if (info)
    c->stack_depth += info->stack_effect;
}

static void emit_op_arg(oak_compiler_t* c,
                        const uint8_t op,
                        const uint8_t arg,
                        const int line)
{
  emit_byte(c, op, line);
  emit_byte(c, arg, line);
  const oak_op_info_t* info = oak_op_get_info(op);
  if (info)
    c->stack_depth += info->stack_effect;
}

static uint8_t make_constant(oak_compiler_t* c, const oak_value_t value)
{
  const size_t idx = oak_chunk_add_constant(c->chunk, value);
  if (idx > 255)
  {
    compiler_error_at(c, NULL, "too many constants in one chunk (max 256)");
    return 0;
  }
  return (uint8_t)idx;
}

static size_t emit_jump(oak_compiler_t* c, const uint8_t op, const int line)
{
  emit_op(c, op, line);
  emit_byte(c, 0xff, line);
  emit_byte(c, 0xff, line);
  return c->chunk->count - 2;
}

static void patch_jump(oak_compiler_t* c, const size_t offset)
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

static void patch_jumps(oak_compiler_t* c, const size_t* jumps, const int count)
{
  for (int i = 0; i < count; ++i)
    patch_jump(c, jumps[i]);
}

static void
emit_loop(oak_compiler_t* c, const size_t loop_start, const int line)
{
  emit_op(c, OAK_OP_LOOP, line);
  const size_t jump = c->chunk->count - loop_start + 2;
  if (jump > 0xffff)
  {
    compiler_error_at(c, NULL, "loop body too large (max 65535 bytes)");
    return;
  }

  emit_byte(c, (uint8_t)(jump >> 8 & 0xff), line);
  emit_byte(c, (uint8_t)(jump >> 0 & 0xff), line);
}

static void emit_pops(oak_compiler_t* c, int count, const int line)
{
  while (count-- > 0)
    emit_op(c, OAK_OP_POP, line);
}

static void emit_loop_jump(oak_compiler_t* c,
                            size_t* jumps,
                            int* count,
                            const int target_depth,
                            const char* keyword)
{
  const int saved_depth = c->stack_depth;
  emit_pops(c, c->stack_depth - target_depth, 0);

  if (*count >= OAK_MAX_BREAKS)
  {
    compiler_error_at(c, NULL,
                      "too many '%s' statements in loop (max %d)",
                      keyword, OAK_MAX_BREAKS);
    c->stack_depth = saved_depth;
    return;
  }
  jumps[(*count)++] = emit_jump(c, OAK_OP_JUMP, 0);
  c->stack_depth = saved_depth;
}

static int
resolve_local(const oak_compiler_t* c, const char* name, const size_t length)
{
  for (int i = c->local_count - 1; i >= 0; --i)
  {
    const oak_local_t* local = &c->locals[i];
    if (local->length == length && memcmp(local->name, name, length) == 0)
      return local->slot;
  }

  return -1;
}

static void add_local(oak_compiler_t* c,
                      const char* name,
                      const size_t length,
                      const int slot,
                      const int mutable)
{
  if (c->local_count >= OAK_MAX_LOCALS)
  {
    compiler_error_at(c, NULL, "too many local variables (max %d)", OAK_MAX_LOCALS);
    return;
  }
  oak_local_t* local = &c->locals[c->local_count++];
  local->name = name;
  local->length = length;
  local->slot = slot;
  local->mutable = mutable;
  local->depth = c->scope_depth;

  oak_chunk_add_debug_local(c->chunk, slot, name, length);
}

static void begin_scope(oak_compiler_t* c)
{
  c->scope_depth++;
}

static void end_scope(oak_compiler_t* c)
{
  while (c->local_count > 0 &&
         c->locals[c->local_count - 1].depth == c->scope_depth)
  {
    emit_op(c, OAK_OP_POP, 0);
    c->local_count--;
  }
  c->scope_depth--;
}

static int
is_local_mutable(const oak_compiler_t* c, const char* name, const size_t length)
{
  for (int i = c->local_count - 1; i >= 0; --i)
  {
    const oak_local_t* local = &c->locals[i];
    if (local->length == length && memcmp(local->name, name, length) == 0)
      return local->mutable;
  }
  return 0;
}

static void compile_node(oak_compiler_t* c, const oak_ast_node_t* node);

static void compile_block(oak_compiler_t* c, const oak_ast_node_t* block)
{
  begin_scope(c);
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &block->children)
  {
    compile_node(c, oak_container_of(pos, oak_ast_node_t, link));
  }
  end_scope(c);
}

static size_t list_length(const oak_ast_node_t* node)
{
  return oak_list_length(&node->children);
}

static uint8_t node_kind_to_op(const oak_node_kind_t kind)
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
      return 0;
  }
}

static void compile_node(oak_compiler_t* c, const oak_ast_node_t* node)
{
  if (!node || c->had_error)
    return;

  switch (node->kind)
  {
    case OAK_NODE_KIND_PROGRAM:
    {
      oak_list_entry_t* pos;
      oak_list_for_each(pos, &node->children)
      {
        const oak_ast_node_t* child =
            oak_container_of(pos, oak_ast_node_t, link);
        compile_node(c, child);
      }
      emit_op(c, OAK_OP_HALT, 0);
      break;
    }
    case OAK_NODE_KIND_INT:
    {
      const int value = oak_token_as_i32(node->token);
      const uint8_t idx = make_constant(c, OAK_VALUE_I32(value));
      const int line = oak_token_line(node->token);
      emit_op_arg(c, OAK_OP_CONSTANT, idx, line);
      break;
    }
    case OAK_NODE_KIND_FLOAT:
    {
      const float value = oak_token_as_f32(node->token);
      const uint8_t idx = make_constant(c, OAK_VALUE_F32(value));
      const int line = oak_token_line(node->token);
      emit_op_arg(c, OAK_OP_CONSTANT, idx, line);
      break;
    }
    case OAK_NODE_KIND_STRING:
    {
      const char* chars = oak_token_buf(node->token);
      const int len = oak_token_size(node->token);
      oak_obj_string_t* str = oak_make_string(chars, len);
      const uint8_t idx = make_constant(c, OAK_VALUE_OBJ(str));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, oak_token_line(node->token));
      break;
    }
    case OAK_NODE_KIND_IDENT:
    {
      const char* name = oak_token_buf(node->token);
      const int len = oak_token_size(node->token);
      const int slot = resolve_local(c, name, len);
      if (slot < 0)
      {
        compiler_error_at(c, node->token, "undefined variable '%.*s'",
                          (int)len, name);
        return;
      }
      emit_op_arg(c, OAK_OP_GET_LOCAL, (uint8_t)slot, oak_token_line(node->token));
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
        compiler_error_at(c, NULL, "'%s' operator not yet implemented",
                          node->kind == OAK_NODE_KIND_BINARY_AND ? "&&" : "||");
        return;
      }

      compile_node(c, node->lhs);
      compile_node(c, node->rhs);
      emit_op(c, node_kind_to_op(node->kind), 0);
      break;
    }
    case OAK_NODE_KIND_UNARY_NEG:
    case OAK_NODE_KIND_UNARY_NOT:
    {
      compile_node(c, node->child);
      emit_op(c, node_kind_to_op(node->kind), 0);
      break;
    }
    case OAK_NODE_KIND_STMT_EXPR:
    {
      const int depth_before = c->stack_depth;
      oak_ast_node_t* expr;
      oak_ast_node_unpack(node, &expr);
      compile_node(c, expr);
      if (c->stack_depth > depth_before)
        emit_op(c, OAK_OP_POP, 0);
      break;
    }
    case OAK_NODE_KIND_STMT_LET_ASSIGNMENT:
    {
      int is_mut = 0;
      const oak_ast_node_t* assign = NULL;

      oak_list_entry_t* pos;
      oak_list_for_each(pos, &node->children)
      {
        const oak_ast_node_t* ch = oak_container_of(pos, oak_ast_node_t, link);
        if (ch->kind == OAK_NODE_KIND_MUT_KEYWORD)
          is_mut = 1;
        else if (ch->kind == OAK_NODE_KIND_STMT_ASSIGNMENT)
          assign = ch;
      }

      if (!assign)
      {
        compiler_error_at(c, NULL, "malformed 'let' statement");
        return;
      }

      const oak_ast_node_t* ident = assign->lhs;
      const oak_ast_node_t* rhs = assign->rhs;

      compile_node(c, rhs);
      const char* name = oak_token_buf(ident->token);
      const int size = oak_token_size(ident->token);
      add_local(c, name, size, c->stack_depth - 1, is_mut);

      break;
    }
    case OAK_NODE_KIND_STMT_ASSIGNMENT:
    {
      const oak_ast_node_t* lhs = node->lhs;
      const oak_ast_node_t* rhs = node->rhs;

      if (lhs->kind != OAK_NODE_KIND_IDENT)
      {
        compiler_error_at(c, lhs->token,
                          "assignment target must be a variable");
        return;
      }

      const int slot = resolve_local(c, oak_token_buf(lhs->token), oak_token_size(lhs->token));
      if (slot < 0)
      {
        compiler_error_at(c, lhs->token, "undefined variable '%.*s'",
                          oak_token_size(lhs->token), oak_token_buf(lhs->token));
        return;
      }

      if (!is_local_mutable(c, oak_token_buf(lhs->token), oak_token_size(lhs->token)))
      {
        compiler_error_at(c, lhs->token,
                          "cannot assign to immutable variable '%.*s'",
                          oak_token_size(lhs->token), oak_token_buf(lhs->token));
        return;
      }

      compile_node(c, rhs);
      emit_op_arg(
          c, OAK_OP_SET_LOCAL, (uint8_t)slot, oak_token_line(lhs->token));
      emit_op(c, OAK_OP_POP, 0);
      break;
    }
    case OAK_NODE_KIND_STMT_ADD_ASSIGN:
    case OAK_NODE_KIND_STMT_SUB_ASSIGN:
    case OAK_NODE_KIND_STMT_MUL_ASSIGN:
    case OAK_NODE_KIND_STMT_DIV_ASSIGN:
    case OAK_NODE_KIND_STMT_MOD_ASSIGN:
    {
      const oak_ast_node_t* lhs = node->lhs;
      if (lhs->kind != OAK_NODE_KIND_IDENT)
      {
        compiler_error_at(c, lhs->token,
                          "compound assignment target must be a variable");
        return;
      }

      const int slot = resolve_local(c, oak_token_buf(lhs->token), oak_token_size(lhs->token));
      if (slot < 0)
      {
        compiler_error_at(c, lhs->token, "undefined variable '%.*s'",
                          oak_token_size(lhs->token), oak_token_buf(lhs->token));
        return;
      }

      if (!is_local_mutable(c, oak_token_buf(lhs->token), oak_token_size(lhs->token)))
      {
        compiler_error_at(c, lhs->token,
                          "cannot assign to immutable variable '%.*s'",
                          oak_token_size(lhs->token), oak_token_buf(lhs->token));
        return;
      }

      emit_op_arg(
          c, OAK_OP_GET_LOCAL, (uint8_t)slot, oak_token_line(lhs->token));
      compile_node(c, node->rhs);
      emit_op(c, node_kind_to_op(node->kind), 0);
      emit_op_arg(
          c, OAK_OP_SET_LOCAL, (uint8_t)slot, oak_token_line(lhs->token));
      emit_op(c, OAK_OP_POP, 0);
      break;
    }
    case OAK_NODE_KIND_FN_CALL:
    {
      oak_ast_node_t* callee;
      oak_ast_node_t* arg;
      oak_ast_node_unpack(node, &callee, &arg);

      if (!callee || callee->kind != OAK_NODE_KIND_IDENT)
      {
        compiler_error_at(c, callee ? callee->token : NULL,
                          "callee must be an identifier");
        return;
      }

      if (oak_token_size(callee->token) == 5 &&
          memcmp(oak_token_buf(callee->token), "print", 5) == 0)
      {
        const size_t argc = list_length(node) - 1;
        if (argc != 1)
        {
          compiler_error_at(c, callee->token,
                            "print() expects 1 argument, got %zu", argc);
          return;
        }
        if (arg->kind == OAK_NODE_KIND_FN_CALL_ARG)
          compile_node(c, arg->child);
        else
          compile_node(c, arg);

        emit_op(c, OAK_OP_PRINT, oak_token_line(callee->token));
        break;
      }

      compiler_error_at(c, callee->token, "undefined function '%.*s'",
                        oak_token_size(callee->token), oak_token_buf(callee->token));
      break;
    }
    case OAK_NODE_KIND_STMT_IF:
    {
      oak_list_entry_t* pos = node->children.next;
      const oak_ast_node_t* cond = oak_container_of(pos, oak_ast_node_t, link);
      pos = pos->next;
      const oak_ast_node_t* body = oak_container_of(pos, oak_ast_node_t, link);
      pos = pos->next;
      const oak_ast_node_t* else_node =
          (pos != &node->children) ? oak_container_of(pos, oak_ast_node_t, link)
                                   : NULL;

      compile_node(c, cond);
      const size_t then_jump = emit_jump(c, OAK_OP_JUMP_IF_FALSE, 0);

      compile_block(c, body);

      if (else_node)
      {
        const size_t else_jump = emit_jump(c, OAK_OP_JUMP, 0);
        patch_jump(c, then_jump);
        compile_block(c, else_node->child);
        patch_jump(c, else_jump);
      }
      else
      {
        patch_jump(c, then_jump);
      }
      break;
    }
    case OAK_NODE_KIND_STMT_WHILE:
    {
      oak_loop_ctx_t loop = {
        .enclosing = c->current_loop,
        .loop_start = c->chunk->count,
        .exit_depth = c->stack_depth,
        .continue_depth = c->stack_depth,
        .break_count = 0,
        .continue_count = 0,
      };

      // ReSharper disable once CppDFALocalValueEscapesFunction
      c->current_loop = &loop;

      compile_node(c, node->lhs);
      const size_t exit_jump = emit_jump(c, OAK_OP_JUMP_IF_FALSE, 0);

      compile_block(c, node->rhs);

      patch_jumps(c, loop.continue_jumps, loop.continue_count);
      emit_loop(c, loop.loop_start, 0);
      patch_jump(c, exit_jump);
      patch_jumps(c, loop.break_jumps, loop.break_count);

      c->current_loop = loop.enclosing;
      break;
    }
    case OAK_NODE_KIND_STMT_FOR_FROM:
    {
      oak_list_entry_t* pos = node->children.next;
      const oak_ast_node_t* ident = oak_container_of(pos, oak_ast_node_t, link);
      pos = pos->next;
      const oak_ast_node_t* from_expr =
          oak_container_of(pos, oak_ast_node_t, link);
      pos = pos->next;
      const oak_ast_node_t* to_expr =
          oak_container_of(pos, oak_ast_node_t, link);
      pos = pos->next;
      const oak_ast_node_t* body = oak_container_of(pos, oak_ast_node_t, link);

      begin_scope(c);

      compile_node(c, from_expr);
      const int i_slot = c->stack_depth - 1;
      add_local(c, oak_token_buf(ident->token), oak_token_size(ident->token), i_slot, 1);

      compile_node(c, to_expr);
      const int limit_slot = c->stack_depth - 1;
      add_local(c, "", 0, limit_slot, 0);

      oak_loop_ctx_t loop = {
        .enclosing = c->current_loop,
        .loop_start = c->chunk->count,
        .exit_depth = c->stack_depth - 2,
        .continue_depth = c->stack_depth,
        .break_count = 0,
        .continue_count = 0,
      };

      // ReSharper disable once CppDFALocalValueEscapesFunction
      c->current_loop = &loop;

      emit_op_arg(c, OAK_OP_GET_LOCAL, (uint8_t)i_slot, 0);
      emit_op_arg(c, OAK_OP_GET_LOCAL, (uint8_t)limit_slot, 0);
      emit_op(c, OAK_OP_LT, 0);
      const size_t exit_jump = emit_jump(c, OAK_OP_JUMP_IF_FALSE, 0);

      compile_block(c, body);

      patch_jumps(c, loop.continue_jumps, loop.continue_count);

      emit_op_arg(c, OAK_OP_GET_LOCAL, (uint8_t)i_slot, 0);
      const uint8_t one_idx = make_constant(c, OAK_VALUE_I32(1));
      emit_op_arg(c, OAK_OP_CONSTANT, one_idx, 0);
      emit_op(c, OAK_OP_ADD, 0);
      emit_op_arg(c, OAK_OP_SET_LOCAL, (uint8_t)i_slot, 0);
      emit_pops(c, 1, 0);

      emit_loop(c, loop.loop_start, 0);

      patch_jump(c, exit_jump);

      end_scope(c);

      patch_jumps(c, loop.break_jumps, loop.break_count);

      c->current_loop = loop.enclosing;
      break;
    }
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
      oak_loop_ctx_t* loop = c->current_loop;
      emit_loop_jump(c,
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

oak_chunk_t* oak_compile(const oak_ast_node_t* root)
{
  oak_chunk_t* chunk = oak_alloc(sizeof(oak_chunk_t), OAK_SRC_LOC);
  oak_chunk_init(chunk);

  oak_compiler_t compiler = {
    .chunk = chunk,
    .local_count = 0,
    .scope_depth = 0,
    .stack_depth = 0,
    .had_error = 0,
    .current_loop = NULL,
  };

  compile_node(&compiler, root);

  if (compiler.had_error)
  {
    oak_chunk_free(chunk);
    return NULL;
  }

  return chunk;
}