#include "oak_compiler.h"

#include "oak_log.h"
#include "oak_mem.h"

#include <string.h>

#define OAK_MAX_LOCALS 256
#define OAK_MAX_BREAKS 64

typedef struct
{
  const char* name;
  size_t length;
  int slot;
} oak_local_t;

typedef struct oak_loop_ctx_t
{
  struct oak_loop_ctx_t* enclosing;
  size_t loop_start;
  int exit_depth;
  int continue_depth;
  size_t break_jumps[OAK_MAX_BREAKS];
  int break_count;
} oak_loop_ctx_t;

typedef struct
{
  oak_chunk_t* chunk;
  oak_local_t locals[OAK_MAX_LOCALS];
  int local_count;
  int stack_depth;
  int had_error;
  oak_loop_ctx_t* current_loop;
} oak_compiler_t;

static void compiler_error(oak_compiler_t* c, const char* msg)
{
  oak_log(OAK_LOG_ERR, "compile error: %s", msg);
  c->had_error = 1;
}

static void
emit_byte(const oak_compiler_t* c, const uint8_t byte, const int line)
{
  oak_chunk_write(c->chunk, byte, line);
}

static void emit_bytes(const oak_compiler_t* c,
                       const uint8_t b1,
                       const uint8_t b2,
                       const int line)
{
  emit_byte(c, b1, line);
  emit_byte(c, b2, line);
}

static uint8_t make_constant(oak_compiler_t* c, const oak_value_t value)
{
  const size_t idx = oak_chunk_add_constant(c->chunk, value);
  if (idx > 255)
  {
    compiler_error(c, "too many constants in one chunk");
    return 0;
  }
  return (uint8_t)idx;
}

static size_t
emit_jump(const oak_compiler_t* c, const uint8_t op, const int line)
{
  emit_byte(c, op, line);
  emit_byte(c, 0xff, line);
  emit_byte(c, 0xff, line);
  return c->chunk->count - 2;
}

static void patch_jump(oak_compiler_t* c, const size_t offset)
{
  const size_t jump = c->chunk->count - offset - 2;
  if (jump > 0xffff)
  {
    compiler_error(c, "jump offset too large");
    return;
  }

  c->chunk->bytecode[offset + 0] = (uint8_t)(jump >> 8 & 0xff);
  c->chunk->bytecode[offset + 1] = (uint8_t)(jump >> 0 & 0xff);
}

static void
emit_loop(oak_compiler_t* c, const size_t loop_start, const int line)
{
  emit_byte(c, OAK_OP_LOOP, line);
  const size_t jump = c->chunk->count - loop_start + 2;
  if (jump > 0xffff)
  {
    compiler_error(c, "loop body too large");
    return;
  }

  emit_byte(c, (uint8_t)(jump >> 8 & 0xff), line);
  emit_byte(c, (uint8_t)(jump >> 0 & 0xff), line);
}

static void emit_pops(oak_compiler_t* c, int count, const int line)
{
  while (count-- > 0)
  {
    emit_byte(c, OAK_OP_POP, line);
    c->stack_depth--;
  }
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
                      const int slot)
{
  if (c->local_count >= OAK_MAX_LOCALS)
  {
    compiler_error(c, "too many local variables");
    return;
  }
  oak_local_t* local = &c->locals[c->local_count++];
  local->name = name;
  local->length = length;
  local->slot = slot;
}

static void compile_node(oak_compiler_t* c, const oak_ast_node_t* node);

static oak_ast_node_t* list_child(const oak_ast_node_t* node, const size_t idx)
{
  size_t i;
  oak_list_entry_t* pos;
  oak_list_for_each_indexed(i, pos, &node->children)
  {
    if (i == idx)
      return oak_container_of(pos, oak_ast_node_t, link);
  }
  return NULL;
}

static size_t list_length(const oak_ast_node_t* node)
{
  return oak_list_length(&node->children);
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
      emit_byte(c, OAK_OP_HALT, 0);
      break;
    }
    case OAK_NODE_KIND_INT:
    {
      const int value = oak_token_as_i32(node->token);
      const uint8_t idx = make_constant(c, OAK_VALUE_I32(value));
      const int line = oak_token_line(node->token);
      emit_bytes(c, OAK_OP_CONSTANT, idx, line);
      c->stack_depth++;
      break;
    }
    case OAK_NODE_KIND_FLOAT:
    {
      const float value = oak_token_as_f32(node->token);
      const uint8_t idx = make_constant(c, OAK_VALUE_F32(value));
      const int line = oak_token_line(node->token);
      emit_bytes(c, OAK_OP_CONSTANT, idx, line);
      c->stack_depth++;
      break;
    }
    case OAK_NODE_KIND_STRING:
    {
      const char* chars = node->token->buf;
      const size_t len = node->token->size;
      oak_obj_string_t* str = oak_make_string(chars, len);
      const uint8_t idx = make_constant(c, OAK_VALUE_OBJ(str));
      emit_bytes(c, OAK_OP_CONSTANT, idx, node->token->line);
      c->stack_depth++;
      break;
    }
    case OAK_NODE_KIND_IDENT:
    {
      const char* name = node->token->buf;
      const size_t len = node->token->size;
      const int slot = resolve_local(c, name, len);
      if (slot < 0)
      {
        compiler_error(c, "undefined variable");
        return;
      }
      emit_bytes(c, OAK_OP_GET_LOCAL, (uint8_t)slot, node->token->line);
      c->stack_depth++;
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
        compiler_error(c, "and/or not yet implemented");
        return;
      }

      compile_node(c, node->lhs);
      compile_node(c, node->rhs);

      uint8_t op = 0;
      switch (node->kind)
      {
        case OAK_NODE_KIND_BINARY_ADD:
          op = OAK_OP_ADD;
          break;
        case OAK_NODE_KIND_BINARY_SUB:
          op = OAK_OP_SUB;
          break;
        case OAK_NODE_KIND_BINARY_MUL:
          op = OAK_OP_MUL;
          break;
        case OAK_NODE_KIND_BINARY_DIV:
          op = OAK_OP_DIV;
          break;
        case OAK_NODE_KIND_BINARY_MOD:
          op = OAK_OP_MOD;
          break;
        case OAK_NODE_KIND_BINARY_EQ:
          op = OAK_OP_EQ;
          break;
        case OAK_NODE_KIND_BINARY_NEQ:
          op = OAK_OP_NEQ;
          break;
        case OAK_NODE_KIND_BINARY_LESS:
          op = OAK_OP_LT;
          break;
        case OAK_NODE_KIND_BINARY_LESS_EQ:
          op = OAK_OP_LE;
          break;
        case OAK_NODE_KIND_BINARY_GREATER:
          op = OAK_OP_GT;
          break;
        case OAK_NODE_KIND_BINARY_GREATER_EQ:
          op = OAK_OP_GE;
          break;
        default:
          compiler_error(c, "unknown binary operator");
          break;
      }

      emit_byte(c, op, 0);
      c->stack_depth--;
      break;
    }
    case OAK_NODE_KIND_UNARY_NEG:
    {
      compile_node(c, node->child);
      emit_byte(c, OAK_OP_NEGATE, 0);
      break;
    }
    case OAK_NODE_KIND_UNARY_NOT:
    {
      compile_node(c, node->child);
      emit_byte(c, OAK_OP_NOT, 0);
      break;
    }
    case OAK_NODE_KIND_STMT_EXPR:
    {
      const int depth_before = c->stack_depth;
      oak_ast_node_t* expr;
      oak_ast_node_unpack(node, &expr);
      compile_node(c, expr);
      if (c->stack_depth > depth_before)
      {
        emit_byte(c, OAK_OP_POP, 0);
        c->stack_depth--;
      }
      break;
    }
    case OAK_NODE_KIND_STMT_LET_ASSIGNMENT:
    {
      const oak_ast_node_t* assign = node->child;
      if (!assign || assign->kind != OAK_NODE_KIND_STMT_ASSIGNMENT)
      {
        compiler_error(c, "malformed let assignment");
        return;
      }

      oak_ast_node_t* ident = assign->lhs;
      oak_ast_node_t* rhs = assign->rhs;

      compile_node(c, rhs);
      const char* name = ident->token->buf;
      const size_t size = ident->token->size;
      add_local(c, name, size, c->stack_depth - 1);

      break;
    }
    case OAK_NODE_KIND_STMT_ASSIGNMENT:
    {
      oak_ast_node_t* lhs = node->lhs;
      oak_ast_node_t* rhs = node->rhs;

      if (lhs->kind != OAK_NODE_KIND_IDENT)
      {
        compiler_error(c, "assignment target must be a variable");
        return;
      }

      const int slot = resolve_local(c, lhs->token->buf, lhs->token->size);
      if (slot < 0)
      {
        compiler_error(c, "undefined variable in assignment");
        return;
      }

      compile_node(c, rhs);
      emit_bytes(
          c, OAK_OP_SET_LOCAL, (uint8_t)slot, oak_token_line(lhs->token));
      emit_byte(c, OAK_OP_POP, 0);
      c->stack_depth--;
      break;
    }
    case OAK_NODE_KIND_STMT_ADD_ASSIGN:
    case OAK_NODE_KIND_STMT_SUB_ASSIGN:
    case OAK_NODE_KIND_STMT_MUL_ASSIGN:
    case OAK_NODE_KIND_STMT_DIV_ASSIGN:
    case OAK_NODE_KIND_STMT_MOD_ASSIGN:
    {
      oak_ast_node_t* lhs = node->lhs;
      if (lhs->kind != OAK_NODE_KIND_IDENT)
      {
        compiler_error(c, "compound assignment target must be a variable");
        return;
      }

      const int slot = resolve_local(c, lhs->token->buf, lhs->token->size);
      if (slot < 0)
      {
        compiler_error(c, "undefined variable in compound assignment");
        return;
      }

      emit_bytes(
          c, OAK_OP_GET_LOCAL, (uint8_t)slot, oak_token_line(lhs->token));
      c->stack_depth++;
      compile_node(c, node->rhs);

      uint8_t op = 0;
      switch (node->kind)
      {
        case OAK_NODE_KIND_STMT_ADD_ASSIGN:
          op = OAK_OP_ADD;
          break;
        case OAK_NODE_KIND_STMT_SUB_ASSIGN:
          op = OAK_OP_SUB;
          break;
        case OAK_NODE_KIND_STMT_MUL_ASSIGN:
          op = OAK_OP_MUL;
          break;
        case OAK_NODE_KIND_STMT_DIV_ASSIGN:
          op = OAK_OP_DIV;
          break;
        case OAK_NODE_KIND_STMT_MOD_ASSIGN:
          op = OAK_OP_MOD;
          break;
        default:
          break;
      }
      emit_byte(c, op, 0);
      c->stack_depth--;

      emit_bytes(
          c, OAK_OP_SET_LOCAL, (uint8_t)slot, oak_token_line(lhs->token));
      emit_byte(c, OAK_OP_POP, 0);
      c->stack_depth--;
      break;
    }
    case OAK_NODE_KIND_FN_CALL:
    {
      oak_ast_node_t* callee;
      oak_ast_node_t* arg;
      oak_ast_node_unpack(node, &callee, &arg);

      if (!callee || callee->kind != OAK_NODE_KIND_IDENT)
      {
        compiler_error(c, "callee must be an identifier");
        return;
      }

      if (callee->token->size == 5 &&
          memcmp(callee->token->buf, "print", 5) == 0)
      {
        const size_t argc = list_length(node) - 1;
        if (argc != 1)
        {
          compiler_error(c, "print expects exactly 1 argument");
          return;
        }
        if (arg->kind == OAK_NODE_KIND_FN_CALL_ARG)
          compile_node(c, arg->rhs);
        else
          compile_node(c, arg);

        emit_byte(c, OAK_OP_PRINT, callee->token->line);
        c->stack_depth--;
        break;
      }

      compiler_error(c, "only built-in print() is supported");
      break;
    }
    case OAK_NODE_KIND_STMT_IF:
    {
      // TODO: Make the code more efficient by avoiding multiple list_child
      // calls
      const size_t count = list_length(node);
      const oak_ast_node_t* cond = list_child(node, 0);

      const oak_ast_node_t* last = list_child(node, count - 1);
      const int has_else = last && last->kind == OAK_NODE_KIND_ELSE_BLOCK;
      const size_t body_end = has_else ? count - 1 : count;

      compile_node(c, cond);
      const size_t then_jump = emit_jump(c, OAK_OP_JUMP_IF_FALSE, 0);
      c->stack_depth--;

      for (size_t i = 1; i < body_end; ++i)
        compile_node(c, list_child(node, i));

      if (has_else)
      {
        const size_t else_jump = emit_jump(c, OAK_OP_JUMP, 0);
        patch_jump(c, then_jump);

        oak_list_entry_t* pos;
        oak_list_for_each(pos, &last->children)
        {
          const oak_ast_node_t* stmt =
              oak_container_of(pos, oak_ast_node_t, link);
          compile_node(c, stmt);
        }
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
      const size_t count = list_length(node);
      const oak_ast_node_t* cond = list_child(node, 0);

      oak_loop_ctx_t loop = {
        .enclosing = c->current_loop,
        .loop_start = c->chunk->count,
        .exit_depth = c->stack_depth,
        .continue_depth = c->stack_depth,
        .break_count = 0,
      };

      // ReSharper disable once CppDFALocalValueEscapesFunction
      c->current_loop = &loop;

      compile_node(c, cond);
      const size_t exit_jump = emit_jump(c, OAK_OP_JUMP_IF_FALSE, 0);
      c->stack_depth--;

      const int body_start_depth = c->stack_depth;
      for (size_t i = 1; i < count; ++i)
        compile_node(c, list_child(node, i));

      emit_pops(c, c->stack_depth - body_start_depth, 0);

      emit_loop(c, loop.loop_start, 0);

      patch_jump(c, exit_jump);

      for (int i = 0; i < loop.break_count; ++i)
        patch_jump(c, loop.break_jumps[i]);

      c->current_loop = loop.enclosing;
      break;
    }
    case OAK_NODE_KIND_STMT_FOR_FROM:
    {
      const size_t count = list_length(node);

      oak_ast_node_t* ident = list_child(node, 0);
      oak_ast_node_t* from_expr = list_child(node, 1);
      oak_ast_node_t* to_expr = list_child(node, 2);

      const int saved_local_count = c->local_count;

      compile_node(c, from_expr);
      const int i_slot = c->stack_depth - 1;
      add_local(c, ident->token->buf, ident->token->size, i_slot);

      compile_node(c, to_expr);
      const int limit_slot = c->stack_depth - 1;

      oak_loop_ctx_t loop = {
        .enclosing = c->current_loop,
        .loop_start = c->chunk->count,
        .exit_depth = c->stack_depth - 2,
        .continue_depth = c->stack_depth,
        .break_count = 0,
      };

      // ReSharper disable once CppDFALocalValueEscapesFunction
      c->current_loop = &loop;

      emit_bytes(c, OAK_OP_GET_LOCAL, (uint8_t)i_slot, 0);
      c->stack_depth++;
      emit_bytes(c, OAK_OP_GET_LOCAL, (uint8_t)limit_slot, 0);
      c->stack_depth++;
      emit_byte(c, OAK_OP_LT, 0);
      c->stack_depth--;
      const size_t exit_jump = emit_jump(c, OAK_OP_JUMP_IF_FALSE, 0);
      c->stack_depth--;

      const int body_start_depth = c->stack_depth;
      for (size_t i = 3; i < count; ++i)
        compile_node(c, list_child(node, i));

      emit_pops(c, c->stack_depth - body_start_depth, 0);

      emit_bytes(c, OAK_OP_GET_LOCAL, (uint8_t)i_slot, 0);
      c->stack_depth++;
      const uint8_t one_idx = make_constant(c, OAK_VALUE_I32(1));
      emit_bytes(c, OAK_OP_CONSTANT, one_idx, 0);
      c->stack_depth++;
      emit_byte(c, OAK_OP_ADD, 0);
      c->stack_depth--;
      emit_bytes(c, OAK_OP_SET_LOCAL, (uint8_t)i_slot, 0);
      emit_byte(c, OAK_OP_POP, 0);
      c->stack_depth--;

      emit_loop(c, loop.loop_start, 0);

      patch_jump(c, exit_jump);

      for (int i = 0; i < loop.break_count; ++i)
        patch_jump(c, loop.break_jumps[i]);

      emit_byte(c, OAK_OP_POP, 0);
      c->stack_depth--;
      emit_byte(c, OAK_OP_POP, 0);
      c->stack_depth--;

      c->local_count = saved_local_count;
      c->current_loop = loop.enclosing;
      break;
    }
    case OAK_NODE_KIND_STMT_BREAK:
    {
      if (!c->current_loop)
      {
        compiler_error(c, "break outside of loop");
        return;
      }
      oak_loop_ctx_t* loop = c->current_loop;

      emit_pops(c, c->stack_depth - loop->exit_depth, 0);

      if (loop->break_count >= OAK_MAX_BREAKS)
      {
        compiler_error(c, "too many break statements in loop");
        return;
      }
      loop->break_jumps[loop->break_count++] = emit_jump(c, OAK_OP_JUMP, 0);
      break;
    }
    case OAK_NODE_KIND_STMT_CONTINUE:
    {
      if (!c->current_loop)
      {
        compiler_error(c, "continue outside of loop");
        return;
      }
      oak_loop_ctx_t* loop = c->current_loop;

      emit_pops(c, c->stack_depth - loop->continue_depth, 0);
      emit_loop(c, loop->loop_start, 0);
      break;
    }
    default:
      compiler_error(c, "unsupported AST node kind");
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