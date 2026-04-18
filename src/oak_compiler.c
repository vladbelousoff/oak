#include "oak_compiler.h"

#include "oak_count_of.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_type.h"
#include "oak_value.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define OAK_MAX_LOCALS   256
#define OAK_MAX_USER_FNS 64
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
  usize length;
  int slot;
  int is_mutable;
  int depth;
  struct oak_type_t type;
};

struct oak_loop_frame_t
{
  struct oak_loop_frame_t* enclosing;
  usize loop_start;
  int exit_depth;
  int continue_depth;
  usize break_jumps[OAK_MAX_LOOP_BRANCHES];
  int break_count;
  usize continue_jumps[OAK_MAX_LOOP_BRANCHES];
  int continue_count;
};

/* decl is null for native (C) builtins registered at compile time. */
struct oak_registered_fn_t
{
  const char* name;
  usize name_len;
  u8 const_idx;
  int arity;
  const struct oak_ast_node_t* decl;
};

struct oak_native_binding_t
{
  const char* name;
  oak_native_fn_t impl;
  int arity;
};

/* Static description of a method bound to a receiver type (e.g. arrays).
 * Methods are not exposed as global functions; they're only reachable
 * through `receiver.name(...)` syntax. */
struct oak_method_binding_t
{
  const char* name;
  usize name_len;
  u8 const_idx;
  /* Includes the implicit receiver. So `arr.push(x)` -> arity 2. */
  int total_arity;
};

#define OAK_MAX_ARRAY_METHODS 8

struct oak_compiler_t
{
  struct oak_chunk_t* chunk;
  struct oak_local_t locals[OAK_MAX_LOCALS];
  int local_count;
  int scope_depth;
  int stack_depth;
  int has_error;
  struct oak_loop_frame_t* current_loop;
  int function_depth;
  struct oak_registered_fn_t fn_registry[OAK_MAX_USER_FNS];
  int fn_registry_count;
  struct oak_type_registry_t type_registry;
  struct oak_method_binding_t array_methods[OAK_MAX_ARRAY_METHODS];
  int array_method_count;
};

/* Convenience: intern a type id from a token spelling. */
static oak_type_id_t intern_type_token(struct oak_compiler_t* c,
                                       const struct oak_token_t* token)
{
  return oak_type_registry_intern(&c->type_registry,
                                  oak_token_buf(token),
                                  (usize)oak_token_size(token));
}

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
                      const u8 byte,
                      const struct oak_code_loc_t loc)
{
  oak_chunk_write(c->chunk, byte, loc);
}

static void emit_op(struct oak_compiler_t* c,
                    const u8 op,
                    const struct oak_code_loc_t loc)
{
  emit_byte(c, op, loc);
  const struct oak_op_info_t* info = oak_op_get_info(op);
  if (info)
    c->stack_depth += info->stack_effect;
}

static void emit_op_arg(struct oak_compiler_t* c,
                        const u8 op,
                        const u8 arg,
                        const struct oak_code_loc_t loc)
{
  emit_byte(c, op, loc);
  emit_byte(c, arg, loc);
  const struct oak_op_info_t* info = oak_op_get_info(op);
  if (info)
    c->stack_depth += info->stack_effect;
}

static u8 intern_constant(struct oak_compiler_t* c,
                               const struct oak_value_t value)
{
  if (c->chunk->const_count >= 256)
  {
    compiler_error_at(c, null, "too many constants in one chunk (max 256)");
    return 0;
  }
  const usize idx = oak_chunk_add_constant(c->chunk, value);
  oak_assert(idx <= 255);
  return (u8)idx;
}

static usize emit_jump(struct oak_compiler_t* c,
                        const u8 op,
                        const struct oak_code_loc_t loc)
{
  emit_op(c, op, loc);
  emit_byte(c, 0xff, loc);
  emit_byte(c, 0xff, loc);
  return c->chunk->count - 2;
}

/* On range error, leaves placeholder operands; do not execute bytecode if
 * has_error. */
static void patch_jump(struct oak_compiler_t* c, const usize offset)
{
  const usize jump = c->chunk->count - offset - 2;
  if (jump > 0xffff)
  {
    compiler_error_at(c, null, "jump offset too large (max 65535 bytes)");
    return;
  }

  c->chunk->bytecode[offset + 0] = (u8)(jump >> 8 & 0xff);
  c->chunk->bytecode[offset + 1] = (u8)(jump >> 0 & 0xff);
}

static void
patch_jumps(struct oak_compiler_t* c, const usize* jumps, const int count)
{
  for (int i = 0; i < count; ++i)
    patch_jump(c, jumps[i]);
}

static void emit_loop(struct oak_compiler_t* c,
                      const usize loop_start,
                      const struct oak_code_loc_t loc)
{
  emit_op(c, OAK_OP_LOOP, loc);
  const usize jump = c->chunk->count - loop_start + 2;
  if (jump > 0xffff)
  {
    compiler_error_at(c, null, "loop body too large (max 65535 bytes)");
    return;
  }

  emit_byte(c, (u8)(jump >> 8 & 0xff), loc);
  emit_byte(c, (u8)(jump >> 0 & 0xff), loc);
}

static void
emit_pops(struct oak_compiler_t* c, int count, const struct oak_code_loc_t loc)
{
  while (count-- > 0)
    emit_op(c, OAK_OP_POP, loc);
}

static void emit_loop_control_jump(struct oak_compiler_t* c,
                                   usize* jumps,
                                   int* count,
                                   const int target_depth,
                                   const char* keyword)
{
  const int saved_depth = c->stack_depth;
  emit_pops(c, c->stack_depth - target_depth, OAK_LOC_SYNTHETIC);

  if (*count >= OAK_MAX_LOOP_BRANCHES)
  {
    compiler_error_at(c,
                      null,
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
                      const usize length,
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
                      const usize length,
                      const int slot,
                      const int is_mutable,
                      const struct oak_type_t type)
{
  if (c->local_count >= OAK_MAX_LOCALS)
  {
    compiler_error_at(
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
  const int slot = find_local(c, name, (usize)name_len, &is_mutable);
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

static int fn_decl_has_receiver(const struct oak_ast_node_t* decl)
{
  const struct oak_list_entry_t* first = decl->children.next;
  if (first == &decl->children)
    return 0;
  const struct oak_ast_node_t* n =
      oak_container_of(first, struct oak_ast_node_t, link);
  return n->kind == OAK_NODE_KIND_FN_RECEIVER;
}

static const struct oak_ast_node_t*
fn_decl_name_node(const struct oak_ast_node_t* decl)
{
  const struct oak_list_entry_t* e = decl->children.next;
  oak_assert(e != &decl->children);
  if (fn_decl_has_receiver(decl))
    e = e->next;
  return oak_container_of(e, struct oak_ast_node_t, link);
}

static const struct oak_ast_node_t*
fn_decl_block(const struct oak_ast_node_t* decl)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &decl->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_KIND_BLOCK)
      return ch;
  }
  return null;
}

static int fn_param_is_mutable(const struct oak_ast_node_t* param)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_KIND_MUT_KEYWORD)
      return 1;
  }
  return 0;
}

static const struct oak_ast_node_t*
fn_param_ident(const struct oak_ast_node_t* param)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_KIND_IDENT)
      return ch;
  }
  return null;
}

static const struct oak_ast_node_t*
fn_param_type_ident(const struct oak_ast_node_t* param)
{
  int ident_index = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_KIND_IDENT)
    {
      if (ident_index == 1)
        return ch;
      ++ident_index;
    }
  }
  return null;
}

static const struct oak_ast_node_t*
fn_decl_param_at(const struct oak_ast_node_t* decl, const int index)
{
  int i = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &decl->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_KIND_FN_PARAM)
    {
      if (i == index)
        return ch;
      ++i;
    }
  }
  return null;
}

static const struct oak_ast_node_t*
fn_decl_return_type_node(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* block = fn_decl_block(decl);
  if (!block)
    return null;
  struct oak_list_entry_t* prev = block->link.prev;
  if (prev == &decl->children)
    return null;
  const struct oak_ast_node_t* before =
      oak_container_of(prev, struct oak_ast_node_t, link);
  if (before->kind == OAK_NODE_KIND_FN_PARAM)
    return null;
  const struct oak_ast_node_t* name = fn_decl_name_node(decl);
  if (before == name)
    return null;
  return before;
}

static int count_fn_params(const struct oak_ast_node_t* decl)
{
  int n = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &decl->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_KIND_FN_PARAM)
      ++n;
  }
  return n;
}

static void register_native_fn(struct oak_compiler_t* c,
                               const struct oak_native_binding_t* binding)
{
  if (c->fn_registry_count >= OAK_MAX_USER_FNS)
  {
    compiler_error_at(c,
                      null,
                      "too many functions in one program (max %d)",
                      OAK_MAX_USER_FNS);
    return;
  }

  struct oak_obj_native_fn_t* native =
      oak_make_native_fn(binding->impl, binding->arity, binding->name);
  const u8 idx = intern_constant(c, OAK_VALUE_OBJ(&native->obj));

  struct oak_registered_fn_t* slot = &c->fn_registry[c->fn_registry_count++];
  slot->name = binding->name;
  slot->name_len = strlen(binding->name);
  slot->const_idx = idx;
  slot->arity = binding->arity;
  slot->decl = null;
}

static const struct oak_native_binding_t native_builtins[] = {
  { "print", oak_builtin_print, 1 },
};

static void register_native_builtins(struct oak_compiler_t* c)
{
  for (usize i = 0; i < oak_count_of(native_builtins); ++i)
  {
    register_native_fn(c, &native_builtins[i]);
    if (c->has_error)
      return;
  }
}

static const struct oak_native_binding_t array_method_bindings[] = {
  /* push(receiver, value) -> new length. */
  { "push", oak_builtin_push, 2 },
  /* len(receiver) -> length. */
  { "len", oak_builtin_len, 1 },
};

static void register_array_methods(struct oak_compiler_t* c)
{
  for (usize i = 0; i < oak_count_of(array_method_bindings); ++i)
  {
    if (c->array_method_count >= OAK_MAX_ARRAY_METHODS)
    {
      compiler_error_at(
          c, null, "too many array methods (max %d)", OAK_MAX_ARRAY_METHODS);
      return;
    }

    const struct oak_native_binding_t* b = &array_method_bindings[i];
    struct oak_obj_native_fn_t* native =
        oak_make_native_fn(b->impl, b->arity, b->name);
    const u8 idx = intern_constant(c, OAK_VALUE_OBJ(&native->obj));
    if (c->has_error)
      return;

    struct oak_method_binding_t* slot =
        &c->array_methods[c->array_method_count++];
    slot->name = b->name;
    slot->name_len = strlen(b->name);
    slot->const_idx = idx;
    slot->total_arity = b->arity;
  }
}

static const struct oak_method_binding_t*
find_array_method(struct oak_compiler_t* c, const char* name, const usize len)
{
  for (int i = 0; i < c->array_method_count; ++i)
  {
    const struct oak_method_binding_t* m = &c->array_methods[i];
    if (m->name_len == len && memcmp(m->name, name, len) == 0)
      return m;
  }
  return null;
}

static void register_program_functions(struct oak_compiler_t* c,
                                       const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_KIND_FN_DECL)
      continue;

    if (fn_decl_has_receiver(item))
    {
      compiler_error_at(
          c, item->token, "methods with a receiver are not implemented yet");
      return;
    }

    const struct oak_ast_node_t* name_node = fn_decl_name_node(item);
    const char* name = oak_token_buf(name_node->token);
    const int name_len = oak_token_size(name_node->token);
    const int arity = count_fn_params(item);

    for (int i = 0; i < c->fn_registry_count; ++i)
    {
      const struct oak_registered_fn_t* e = &c->fn_registry[i];
      if (e->name_len == (usize)name_len &&
          memcmp(e->name, name, (usize)name_len) == 0)
      {
        compiler_error_at(
            c, name_node->token, "duplicate function '%.*s'", name_len, name);
        return;
      }
    }

    if (c->fn_registry_count >= OAK_MAX_USER_FNS)
    {
      compiler_error_at(c,
                        null,
                        "too many functions in one program (max %d)",
                        OAK_MAX_USER_FNS);
      return;
    }

    struct oak_obj_fn_t* fn_obj = oak_make_fn(0, arity);
    const u8 idx = intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

    struct oak_registered_fn_t* slot = &c->fn_registry[c->fn_registry_count++];
    slot->name = name;
    slot->name_len = (usize)name_len;
    slot->const_idx = idx;
    slot->arity = arity;
    slot->decl = item;
  }
}

static const struct oak_registered_fn_t* find_registered_fn_entry(
    struct oak_compiler_t* c, const char* name, const usize len)
{
  for (int i = 0; i < c->fn_registry_count; ++i)
  {
    const struct oak_registered_fn_t* e = &c->fn_registry[i];
    if (e->name_len == len && memcmp(e->name, name, len) == 0)
      return e;
  }
  return null;
}

static int find_registered_fn(struct oak_compiler_t* c,
                              const char* name,
                              const usize len,
                              u8* out_idx,
                              int* out_arity)
{
  const struct oak_registered_fn_t* e = find_registered_fn_entry(c, name, len);
  if (!e)
    return 0;
  *out_idx = e->const_idx;
  *out_arity = e->arity;
  return 1;
}

static void compile_stmt_return(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* node)
{
  if (c->function_depth == 0)
  {
    compiler_error_at(c, null, "'return' outside of a function");
    return;
  }

  struct oak_ast_node_t* expr = null;
  oak_ast_node_unpack(node, &expr);
  if (expr)
    compile_node(c, expr);
  else
  {
    const u8 z = intern_constant(c, OAK_VALUE_I32(0));
    emit_op_arg(c, OAK_OP_CONSTANT, z, OAK_LOC_SYNTHETIC);
  }
  emit_op(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);
}

static void compile_function_body(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* body = fn_decl_block(decl);
  if (!body || body->kind != OAK_NODE_KIND_BLOCK)
  {
    compiler_error_at(c, decl->token, "function has no body");
    return;
  }

  c->function_depth++;
  c->local_count = 0;
  c->scope_depth = 0;
  c->stack_depth = 0;
  c->current_loop = null;

  struct oak_list_entry_t* pos;
  int slot = 0;
  oak_list_for_each(pos, &decl->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind != OAK_NODE_KIND_FN_PARAM)
      continue;
    const struct oak_ast_node_t* id = fn_param_ident(ch);
    if (!id)
    {
      compiler_error_at(c, ch->token, "malformed function parameter");
      c->function_depth--;
      return;
    }
    const struct oak_ast_node_t* type_id = fn_param_type_ident(ch);
    struct oak_type_t param_type;
    oak_type_clear(&param_type);
    if (type_id && type_id->kind == OAK_NODE_KIND_IDENT)
      param_type.id = intern_type_token(c, type_id->token);
    add_local(c,
              oak_token_buf(id->token),
              (usize)oak_token_size(id->token),
              slot++,
              fn_param_is_mutable(ch),
              param_type);
  }

  const int arity = count_fn_params(decl);
  c->stack_depth = arity;

  compile_block(c, body);

  const u8 z = intern_constant(c, OAK_VALUE_I32(0));
  emit_op_arg(c, OAK_OP_CONSTANT, z, OAK_LOC_SYNTHETIC);
  emit_op(c, OAK_OP_RETURN, OAK_LOC_SYNTHETIC);

  c->function_depth--;
}

static void compile_function_bodies(struct oak_compiler_t* c)
{
  for (int i = 0; i < c->fn_registry_count; ++i)
  {
    const struct oak_registered_fn_t* e = &c->fn_registry[i];
    if (!e->decl)
      continue;
    struct oak_value_t fn_val = c->chunk->constants[e->const_idx];
    struct oak_obj_fn_t* fn_obj = oak_as_fn(fn_val);
    fn_obj->code_offset = c->chunk->count;
    compile_function_body(c, e->decl);
    if (c->has_error)
      return;
  }
}

static void compile_program_items(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind == OAK_NODE_KIND_FN_DECL)
      continue;
    compile_node(c, item);
  }
}

static void compile_program(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* program)
{
  register_native_builtins(c);
  if (c->has_error)
    return;
  register_array_methods(c);
  if (c->has_error)
    return;
  register_program_functions(c, program);
  if (c->has_error)
    return;
  compile_program_items(c, program);
  if (c->has_error)
    return;
  emit_op(c, OAK_OP_HALT, OAK_LOC_SYNTHETIC);
  compile_function_bodies(c);
}

static usize ast_child_count(const struct oak_ast_node_t* node)
{
  if (oak_node_grammar_op_unary(node->kind))
    return node->child ? 1u : 0u;
  if (oak_node_grammar_op_binary(node->kind))
    return (usize)(node->lhs ? 1 : 0) + (usize)(node->rhs ? 1 : 0);
  return oak_list_length(&node->children);
}

static int ast_is_int_literal(const struct oak_ast_node_t* node,
                              const int value)
{
  return node && node->kind == OAK_NODE_KIND_INT &&
         oak_token_as_i32(node->token) == value;
}

static u8 opcode_for_node_kind(const enum oak_node_kind_t kind)
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

static int local_type_get(struct oak_compiler_t* c,
                          const char* name,
                          const usize len,
                          struct oak_type_t* out)
{
  for (int i = c->local_count - 1; i >= 0; --i)
  {
    const struct oak_local_t* L = &c->locals[i];
    if (L->length == len && memcmp(L->name, name, len) == 0)
    {
      *out = L->type;
      return 1;
    }
  }
  return 0;
}

static void infer_expr_static_type(struct oak_compiler_t* c,
                                   const struct oak_ast_node_t* expr,
                                   struct oak_type_t* out)
{
  oak_type_clear(out);
  if (!expr)
    return;

  switch (expr->kind)
  {
    case OAK_NODE_KIND_INT:
    case OAK_NODE_KIND_FLOAT:
    case OAK_NODE_KIND_UNARY_NEG:
    case OAK_NODE_KIND_BINARY_ADD:
    case OAK_NODE_KIND_BINARY_SUB:
    case OAK_NODE_KIND_BINARY_MUL:
    case OAK_NODE_KIND_BINARY_DIV:
    case OAK_NODE_KIND_BINARY_MOD:
      out->id = OAK_TYPE_NUMBER;
      return;
    case OAK_NODE_KIND_STRING:
      out->id = OAK_TYPE_STRING;
      return;
    case OAK_NODE_KIND_UNARY_NOT:
    case OAK_NODE_KIND_BINARY_EQ:
    case OAK_NODE_KIND_BINARY_NEQ:
    case OAK_NODE_KIND_BINARY_LESS:
    case OAK_NODE_KIND_BINARY_LESS_EQ:
    case OAK_NODE_KIND_BINARY_GREATER:
    case OAK_NODE_KIND_BINARY_GREATER_EQ:
      out->id = OAK_TYPE_BOOL;
      return;
    case OAK_NODE_KIND_IDENT:
    {
      const char* name = oak_token_buf(expr->token);
      const usize len = (usize)oak_token_size(expr->token);
      struct oak_type_t local_ty;
      oak_type_clear(&local_ty);
      if (local_type_get(c, name, len, &local_ty))
        *out = local_ty;
      return;
    }
    case OAK_NODE_KIND_FN_CALL:
    {
      const struct oak_list_entry_t* first = expr->children.next;
      if (first == &expr->children)
        return;
      const struct oak_ast_node_t* callee =
          oak_container_of(first, struct oak_ast_node_t, link);
      if (!callee)
        return;
      if (callee->kind == OAK_NODE_KIND_MEMBER_ACCESS)
      {
        /* Methods on arrays (`push`, `len`) currently all yield numbers. */
        const struct oak_ast_node_t* recv = callee->lhs;
        const struct oak_ast_node_t* method = callee->rhs;
        if (!recv || !method || method->kind != OAK_NODE_KIND_IDENT)
          return;
        struct oak_type_t recv_ty;
        infer_expr_static_type(c, recv, &recv_ty);
        if (!recv_ty.is_array)
          return;
        const struct oak_method_binding_t* m =
            find_array_method(c,
                              oak_token_buf(method->token),
                              (usize)oak_token_size(method->token));
        if (m)
          out->id = OAK_TYPE_NUMBER;
        return;
      }
      if (callee->kind != OAK_NODE_KIND_IDENT)
        return;
      const char* cn = oak_token_buf(callee->token);
      const usize clen = (usize)oak_token_size(callee->token);
      const struct oak_registered_fn_t* fe =
          find_registered_fn_entry(c, cn, clen);
      if (!fe || !fe->decl)
        return;
      const struct oak_ast_node_t* ret = fn_decl_return_type_node(fe->decl);
      if (ret && ret->kind == OAK_NODE_KIND_IDENT)
        out->id = intern_type_token(c, ret->token);
      return;
    }
    case OAK_NODE_KIND_EXPR_CAST:
    {
      const struct oak_ast_node_t* type_node = expr->rhs;
      if (!type_node)
        return;
      if (type_node->kind == OAK_NODE_KIND_TYPE_ARRAY)
      {
        const struct oak_ast_node_t* elem = type_node->child;
        if (!elem || elem->kind != OAK_NODE_KIND_IDENT)
          return;
        out->id = intern_type_token(c, elem->token);
        out->is_array = 1;
        return;
      }
      if (type_node->kind == OAK_NODE_KIND_IDENT)
        out->id = intern_type_token(c, type_node->token);
      return;
    }
    case OAK_NODE_KIND_INDEX_ACCESS:
    {
      struct oak_type_t arr_ty;
      infer_expr_static_type(c, expr->lhs, &arr_ty);
      if (arr_ty.is_array && oak_type_is_known(&arr_ty))
      {
        out->id = arr_ty.id;
        out->is_array = 0;
      }
      return;
    }
    default:
      return;
  }
}

static const char* type_kind_name(struct oak_compiler_t* c,
                                  const struct oak_type_t t)
{
  return oak_type_registry_name(&c->type_registry, t.id);
}

static void
validate_user_fn_call_arg_types(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* call,
                                const struct oak_registered_fn_t* fn)
{
  if (!fn->decl)
    return;
  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  usize i = 0;
  for (; pos != &call->children; pos = pos->next, ++i)
  {
    const struct oak_ast_node_t* arg_wrap =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_KIND_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    const struct oak_ast_node_t* param = fn_decl_param_at(fn->decl, (int)i);
    if (!param)
    {
      compiler_error_at(c, null, "internal error: missing parameter %zu", i);
      return;
    }
    const struct oak_ast_node_t* want_type_node = fn_param_type_ident(param);
    if (!want_type_node || want_type_node->kind != OAK_NODE_KIND_IDENT)
    {
      compiler_error_at(
          c, param->token, "malformed function parameter (expected type name)");
      return;
    }
    const struct oak_type_t want = {
      .id = intern_type_token(c, want_type_node->token),
      .is_array = 0,
    };

    struct oak_type_t got;
    infer_expr_static_type(c, arg_expr, &got);

    if (!oak_type_is_known(&got))
      continue;

    if (!oak_type_equal(&want, &got))
    {
      const struct oak_token_t* err_tok = arg_expr->token;
      if (!err_tok && arg_wrap->kind == OAK_NODE_KIND_FN_CALL_ARG &&
          arg_wrap->child && arg_wrap->child->token)
        err_tok = arg_wrap->child->token;
      compiler_error_at(c,
                        err_tok,
                        "argument %zu: expected type '%s', found '%s%s'",
                        i + 1,
                        type_kind_name(c, want),
                        type_kind_name(c, got),
                        got.is_array ? "[]" : "");
    }
  }
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
          : null;

  compile_node(c, cond);
  const usize then_jump =
      emit_jump(c, OAK_OP_JUMP_IF_FALSE, OAK_LOC_SYNTHETIC);

  compile_block(c, body);

  if (else_node)
  {
    const usize else_jump = emit_jump(c, OAK_OP_JUMP, OAK_LOC_SYNTHETIC);
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
    compiler_error_at(c, null, "malformed 'while' statement");
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
  const usize exit_jump =
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

  struct oak_type_t from_ty;
  infer_expr_static_type(c, from_expr, &from_ty);
  if (!oak_type_is_known(&from_ty))
  {
    from_ty.id = OAK_TYPE_NUMBER;
    from_ty.is_array = 0;
  }

  compile_node(c, from_expr);
  const int loop_var_slot = c->stack_depth - 1;
  add_local(c,
            oak_token_buf(ident->token),
            oak_token_size(ident->token),
            loop_var_slot,
            1,
            from_ty);

  struct oak_type_t to_ty;
  infer_expr_static_type(c, to_expr, &to_ty);
  if (!oak_type_is_known(&to_ty))
  {
    to_ty.id = OAK_TYPE_NUMBER;
    to_ty.is_array = 0;
  }

  compile_node(c, to_expr);
  const int limit_slot = c->stack_depth - 1;
  add_local(c, "", 0, limit_slot, 0, to_ty);

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
    emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)loop_var_slot, ident_loc);
    emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)limit_slot, ident_loc);
    emit_op(c, OAK_OP_LT, ident_loc);
    const usize exit_jump = emit_jump(c, OAK_OP_JUMP_IF_FALSE, ident_loc);

    compile_block(c, body);

    patch_jumps(c, loop.continue_jumps, loop.continue_count);

    emit_op_arg(c, OAK_OP_INC_LOCAL, (u8)loop_var_slot, ident_loc);

    emit_loop(c, loop.loop_start, ident_loc);
    patch_jump(c, exit_jump);
  }

  end_scope(c);

  patch_jumps(c, loop.break_jumps, loop.break_count);

  c->current_loop = loop.enclosing;
}

static void compile_fn_call_arg(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* arg)
{
  if (arg->kind == OAK_NODE_KIND_FN_CALL_ARG)
    compile_node(c, arg->child);
  else
    compile_node(c, arg);
}

static const struct oak_ast_node_t*
fn_call_arg_expr_at(const struct oak_ast_node_t* call, const usize index)
{
  const struct oak_list_entry_t* first = call->children.next;
  if (first == &call->children)
    return null;
  const struct oak_list_entry_t* pos = first->next;
  usize i = 0;
  for (; pos != &call->children; pos = pos->next, ++i)
  {
    if (i != index)
      continue;
    const struct oak_ast_node_t* arg =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (arg->kind == OAK_NODE_KIND_FN_CALL_ARG)
      return arg->child;
    return arg;
  }
  return null;
}

static void validate_array_push_args(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* call,
                                     const struct oak_type_t recv_ty,
                                     const struct oak_token_t* err_tok)
{
  const struct oak_ast_node_t* val_expr = fn_call_arg_expr_at(call, 0);
  if (!val_expr)
    return;

  struct oak_type_t val_ty;
  infer_expr_static_type(c, val_expr, &val_ty);
  if (!oak_type_is_known(&val_ty))
    return;

  const struct oak_type_t element_ty = { .id = recv_ty.id, .is_array = 0 };
  if (!oak_type_equal(&element_ty, &val_ty))
  {
    compiler_error_at(c,
                      val_expr->token ? val_expr->token : err_tok,
                      "cannot push value of type '%s%s' to array of '%s'",
                      type_kind_name(c, val_ty),
                      val_ty.is_array ? "[]" : "",
                      type_kind_name(c, element_ty));
  }
}

/* Compile `receiver.method(args...)`. Method calls are dispatched purely
 * statically based on the receiver's compile-time type. The method's
 * native function is pushed as a constant, the receiver is compiled as
 * an implicit first argument, and finally OP_CALL with the full arity
 * is emitted. */
static void compile_method_call(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* node,
                                const struct oak_ast_node_t* callee)
{
  const struct oak_ast_node_t* receiver = callee->lhs;
  const struct oak_ast_node_t* method = callee->rhs;
  if (!receiver || !method || method->kind != OAK_NODE_KIND_IDENT)
  {
    compiler_error_at(
        c, callee->token, "method call requires 'receiver.name(...)' form");
    return;
  }

  const struct oak_code_loc_t call_loc = code_loc_from_token(method->token);
  const usize user_argc = ast_child_count(node) - 1;
  const char* mname = oak_token_buf(method->token);
  const int mname_len = oak_token_size(method->token);

  struct oak_type_t recv_ty;
  infer_expr_static_type(c, receiver, &recv_ty);

  if (!recv_ty.is_array || !oak_type_is_known(&recv_ty))
  {
    compiler_error_at(c,
                      receiver->token ? receiver->token : method->token,
                      "method '.%.*s' requires a typed array receiver",
                      mname_len,
                      mname);
    return;
  }

  const struct oak_method_binding_t* m =
      find_array_method(c, mname, (usize)mname_len);
  if (!m)
  {
    compiler_error_at(c,
                      method->token,
                      "no method '%.*s' on array of '%s'",
                      mname_len,
                      mname,
                      type_kind_name(c, recv_ty));
    return;
  }

  const int expected_user_argc = m->total_arity - 1;
  if ((int)user_argc != expected_user_argc)
  {
    compiler_error_at(c,
                      method->token,
                      "method '%.*s' expects %d arguments, got %zu",
                      mname_len,
                      mname,
                      expected_user_argc,
                      user_argc);
    return;
  }

  if (m->name_len == 4 && memcmp(m->name, "push", 4) == 0)
  {
    validate_array_push_args(c, node, recv_ty, method->token);
    if (c->has_error)
      return;
  }

  emit_op_arg(c, OAK_OP_CONSTANT, m->const_idx, call_loc);
  compile_node(c, receiver);

  const struct oak_list_entry_t* first = node->children.next;
  for (struct oak_list_entry_t* pos = first->next; pos != &node->children;
       pos = pos->next)
  {
    const struct oak_ast_node_t* arg =
        oak_container_of(pos, struct oak_ast_node_t, link);
    compile_fn_call_arg(c, arg);
  }

  emit_op_arg(c, OAK_OP_CALL, (u8)m->total_arity, call_loc);
  c->stack_depth -= m->total_arity;
}

static void compile_fn_call(struct oak_compiler_t* c,
                            const struct oak_ast_node_t* node)
{
  const struct oak_list_entry_t* first = node->children.next;
  if (first == &node->children)
  {
    compiler_error_at(c, null, "malformed call (no callee)");
    return;
  }

  const struct oak_ast_node_t* callee =
      oak_container_of(first, struct oak_ast_node_t, link);

  if (callee && callee->kind == OAK_NODE_KIND_MEMBER_ACCESS)
  {
    compile_method_call(c, node, callee);
    return;
  }

  if (!callee || callee->kind != OAK_NODE_KIND_IDENT)
  {
    compiler_error_at(
        c, callee ? callee->token : null, "callee must be an identifier");
    return;
  }

  const struct oak_code_loc_t call_loc = code_loc_from_token(callee->token);
  const usize argc = ast_child_count(node) - 1;

  const struct oak_registered_fn_t* entry = find_registered_fn_entry(
      c, oak_token_buf(callee->token), (usize)oak_token_size(callee->token));
  if (!entry)
  {
    compiler_error_at(c,
                      callee->token,
                      "undefined function '%.*s'",
                      oak_token_size(callee->token),
                      oak_token_buf(callee->token));
    return;
  }

  if ((int)argc != entry->arity)
  {
    compiler_error_at(c,
                      callee->token,
                      "function '%.*s' expects %d arguments, got %zu",
                      oak_token_size(callee->token),
                      oak_token_buf(callee->token),
                      entry->arity,
                      argc);
    return;
  }

  validate_user_fn_call_arg_types(c, node, entry);
  if (c->has_error)
    return;

  emit_op_arg(c, OAK_OP_CONSTANT, entry->const_idx, call_loc);

  struct oak_list_entry_t* pos;
  for (pos = first->next; pos != &node->children; pos = pos->next)
  {
    const struct oak_ast_node_t* arg =
        oak_container_of(pos, struct oak_ast_node_t, link);
    compile_fn_call_arg(c, arg);
  }

  emit_op_arg(c, OAK_OP_CALL, (u8)argc, call_loc);
  c->stack_depth -= argc;
}

static void compile_node(struct oak_compiler_t* c,
                         const struct oak_ast_node_t* node)
{
  if (!node || c->has_error)
    return;

  switch (node->kind)
  {
    case OAK_NODE_KIND_PROGRAM:
      compiler_error_at(
          c, null, "internal error: nested program in compilation");
      break;
    case OAK_NODE_KIND_FN_DECL:
      break;
    case OAK_NODE_KIND_STMT_RETURN:
      compile_stmt_return(c, node);
      break;
    case OAK_NODE_KIND_INT:
    {
      const int value = oak_token_as_i32(node->token);
      const u8 idx = intern_constant(c, OAK_VALUE_I32(value));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_KIND_FLOAT:
    {
      const float value = oak_token_as_f32(node->token);
      const u8 idx = intern_constant(c, OAK_VALUE_F32(value));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_KIND_STRING:
    {
      if (c->chunk->const_count >= 256)
      {
        compiler_error_at(c, null, "too many constants in one chunk (max 256)");
        return;
      }
      const char* chars = oak_token_buf(node->token);
      const int len = oak_token_size(node->token);
      struct oak_obj_string_t* str = oak_make_string(chars, (usize)len);
      const u8 idx = intern_constant(c, OAK_VALUE_OBJ(str));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_KIND_IDENT:
    {
      const char* name = oak_token_buf(node->token);
      const int len = oak_token_size(node->token);
      const int slot = find_local(c, name, (usize)len, null);
      if (slot < 0)
      {
        compiler_error_at(
            c, node->token, "undefined variable '%.*s'", (int)len, name);
        return;
      }
      emit_op_arg(
          c, OAK_OP_GET_LOCAL, (u8)slot, code_loc_from_token(node->token));
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
                          null,
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
      const struct oak_ast_node_t* assign = null;

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
        compiler_error_at(c, null, "malformed 'let' statement");
        return;
      }

      const struct oak_ast_node_t* ident = assign->lhs;
      const struct oak_ast_node_t* rhs = assign->rhs;

      struct oak_type_t rhs_ty;
      infer_expr_static_type(c, rhs, &rhs_ty);

      compile_node(c, rhs);
      const char* name = oak_token_buf(ident->token);
      const int size = oak_token_size(ident->token);
      add_local(
          c, name, (usize)size, c->stack_depth - 1, is_mutable, rhs_ty);

      break;
    }
    case OAK_NODE_KIND_STMT_ASSIGNMENT:
    {
      const struct oak_ast_node_t* lhs = node->lhs;
      const struct oak_ast_node_t* rhs = node->rhs;

      if (lhs->kind == OAK_NODE_KIND_INDEX_ACCESS)
      {
        struct oak_type_t arr_ty;
        infer_expr_static_type(c, lhs->lhs, &arr_ty);
        if (!arr_ty.is_array || !oak_type_is_known(&arr_ty))
        {
          compiler_error_at(c,
                            lhs->lhs->token,
                            "indexed assignment requires a typed array");
          return;
        }

        struct oak_type_t val_ty;
        infer_expr_static_type(c, rhs, &val_ty);
        if (oak_type_is_known(&val_ty))
        {
          const struct oak_type_t element_ty = {
            .id = arr_ty.id,
            .is_array = 0,
          };
          if (!oak_type_equal(&element_ty, &val_ty))
          {
            compiler_error_at(
                c,
                rhs->token,
                "cannot assign value of type '%s%s' to element of '%s' "
                "array",
                type_kind_name(c, val_ty),
                val_ty.is_array ? "[]" : "",
                type_kind_name(c, element_ty));
            return;
          }
        }

        compile_node(c, lhs->lhs);
        compile_node(c, lhs->rhs);
        compile_node(c, rhs);
        emit_op(c, OAK_OP_SET_INDEX, OAK_LOC_SYNTHETIC);
        emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
        break;
      }

      const int slot =
          compile_assign_target(c, lhs, "assignment target must be a variable");
      if (slot < 0)
        return;

      compile_node(c, rhs);
      emit_op_arg(
          c, OAK_OP_SET_LOCAL, (u8)slot, code_loc_from_token(lhs->token));
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

      if (node->kind == OAK_NODE_KIND_STMT_ADD_ASSIGN &&
          ast_is_int_literal(node->rhs, 1))
      {
        emit_op_arg(c,
                    OAK_OP_INC_LOCAL,
                    (u8)slot,
                    code_loc_from_token(lhs->token));
        break;
      }
      if (node->kind == OAK_NODE_KIND_STMT_SUB_ASSIGN &&
          ast_is_int_literal(node->rhs, 1))
      {
        emit_op_arg(c,
                    OAK_OP_DEC_LOCAL,
                    (u8)slot,
                    code_loc_from_token(lhs->token));
        break;
      }

      emit_op_arg(
          c, OAK_OP_GET_LOCAL, (u8)slot, code_loc_from_token(lhs->token));
      compile_node(c, node->rhs);
      emit_op(
          c, opcode_for_node_kind(node->kind), code_loc_from_token(lhs->token));
      emit_op_arg(
          c, OAK_OP_SET_LOCAL, (u8)slot, code_loc_from_token(lhs->token));
      emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_KIND_FN_CALL:
      compile_fn_call(c, node);
      break;
    case OAK_NODE_KIND_EXPR_EMPTY_ARRAY:
      compiler_error_at(
          c,
          null,
          "untyped array literal; arrays must be typed (e.g. '[] as "
          "number[]')");
      break;
    case OAK_NODE_KIND_EXPR_CAST:
    {
      const struct oak_ast_node_t* value = node->lhs;
      const struct oak_ast_node_t* type_node = node->rhs;
      if (!value || !type_node)
      {
        compiler_error_at(c, null, "malformed 'as' expression");
        return;
      }

      if (type_node->kind == OAK_NODE_KIND_TYPE_ARRAY)
      {
        const struct oak_ast_node_t* elem = type_node->child;
        if (!elem || elem->kind != OAK_NODE_KIND_IDENT)
        {
          compiler_error_at(
              c, null, "array cast requires an element type (e.g. 'number[]')");
          return;
        }
        if (value->kind != OAK_NODE_KIND_EXPR_EMPTY_ARRAY)
        {
          compiler_error_at(c,
                            null,
                            "only empty array literals can be cast to an "
                            "array type (e.g. '[] as number[]')");
          return;
        }
        emit_op(c, OAK_OP_NEW_ARRAY, OAK_LOC_SYNTHETIC);
        break;
      }

      compiler_error_at(c,
                        null,
                        "'as' is currently only supported for typing array "
                        "literals (e.g. '[] as number[]')");
      break;
    }
    case OAK_NODE_KIND_INDEX_ACCESS:
    {
      compile_node(c, node->lhs);
      compile_node(c, node->rhs);
      emit_op(c, OAK_OP_GET_INDEX, OAK_LOC_SYNTHETIC);
      break;
    }
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
        compiler_error_at(c, null, "'%s' used outside of a loop", keyword);
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
      compiler_error_at(c, null, "unsupported AST node kind (%d)", node->kind);
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
    .current_loop = null,
    .function_depth = 0,
    .fn_registry_count = 0,
    .array_method_count = 0,
  };
  oak_type_registry_init(&compiler.type_registry);

  if (!root || root->kind != OAK_NODE_KIND_PROGRAM)
  {
    compiler_error_at(&compiler, null, "expected a program root");
    oak_chunk_free(chunk);
    return null;
  }

  compile_program(&compiler, root);

  if (compiler.has_error)
  {
    oak_chunk_free(chunk);
    return null;
  }

  return chunk;
}
