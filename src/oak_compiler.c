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
  int arity_min;
  int arity_max;
  const struct oak_ast_node_t* decl;
};

struct oak_native_binding_t
{
  const char* name;
  oak_native_fn_t impl;
  int arity_min;
  int arity_max;
};

struct oak_compiler_t;
struct oak_ast_node_t;
struct oak_token_t;
struct oak_type_t;

/* Optional compile-time argument validator for a method binding. Receives the
 * full call AST node (children are: callee, then user args), the inferred
 * receiver type, and a fallback token to attribute errors to when an arg has
 * no token of its own. */
typedef void (*oak_method_validate_args_fn)(struct oak_compiler_t* c,
                                            const struct oak_ast_node_t* call,
                                            struct oak_type_t recv_ty,
                                            const struct oak_token_t* err_tok);

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
  /* Compile-time return type of this method (always a built-in id). */
  oak_type_id_t return_type_id;
  /* Optional. Called after arity is verified, before bytecode is emitted. */
  oak_method_validate_args_fn validate_args;
};

#define OAK_MAX_ARRAY_METHODS  8
#define OAK_MAX_MAP_METHODS    8
#define OAK_MAX_STRUCTS        32
#define OAK_MAX_STRUCT_FIELDS  32
#define OAK_MAX_STRUCT_METHODS 16

struct oak_struct_field_t
{
  /* Borrowed pointer into the lexer arena (lives for the compilation). */
  const char* name;
  usize name_len;
  struct oak_type_t type;
};

/* User-defined method bound to a struct type. The method's compiled body is
 * stored in the chunk's constants table at `const_idx`; it is invoked like a
 * regular function with the receiver passed as the first argument (slot 0
 * inside the body, accessible as `self`). */
struct oak_struct_method_t
{
  const char* name;
  usize name_len;
  u8 const_idx;
  /* Total arity including the implicit `self` receiver. */
  int arity;
  const struct oak_ast_node_t* decl;
};

struct oak_registered_struct_t
{
  const char* name;
  usize name_len;
  oak_type_id_t type_id;
  int field_count;
  struct oak_struct_field_t fields[OAK_MAX_STRUCT_FIELDS];
  int method_count;
  struct oak_struct_method_t methods[OAK_MAX_STRUCT_METHODS];
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
  int function_depth;
  struct oak_registered_fn_t fn_registry[OAK_MAX_USER_FNS];
  int fn_registry_count;
  struct oak_type_registry_t type_registry;
  struct oak_method_binding_t array_methods[OAK_MAX_ARRAY_METHODS];
  int array_method_count;
  struct oak_method_binding_t map_methods[OAK_MAX_MAP_METHODS];
  int map_method_count;
  struct oak_registered_struct_t structs[OAK_MAX_STRUCTS];
  int struct_count;
};

/* Convenience: intern a type id from a token spelling. */
static oak_type_id_t intern_type_token(struct oak_compiler_t* c,
                                       const struct oak_token_t* token)
{
  return oak_type_registry_intern(&c->type_registry,
                                  oak_token_text(token),
                                  (usize)oak_token_length(token));
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
    oak_log(OAK_LOG_ERROR,
            "%d:%d: error: %s",
            oak_token_line(token),
            oak_token_column(token),
            buf);
  else
    oak_log(OAK_LOG_ERROR, "error: %s", buf);
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
  if (lhs->kind != OAK_NODE_IDENT)
  {
    compiler_error_at(c, lhs->token, "%s", non_ident_msg);
    return -1;
  }
  const char* name = oak_token_text(lhs->token);
  const int name_len = oak_token_length(lhs->token);
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
  return n->kind == OAK_NODE_FN_RECEIVER;
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
fn_decl_self_param(const struct oak_ast_node_t* decl)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &decl->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM_SELF)
      return ch;
  }
  return null;
}

static int fn_param_self_is_mutable(const struct oak_ast_node_t* self_param)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &self_param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_MUT_KEYWORD)
      return 1;
  }
  return 0;
}

static const struct oak_ast_node_t*
fn_decl_block(const struct oak_ast_node_t* decl)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &decl->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_BLOCK)
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
    if (ch->kind == OAK_NODE_MUT_KEYWORD)
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
    if (ch->kind == OAK_NODE_IDENT)
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
    if (ch->kind == OAK_NODE_IDENT)
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
    if (ch->kind == OAK_NODE_FN_PARAM)
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
  if (before->kind == OAK_NODE_FN_PARAM)
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
    if (ch->kind == OAK_NODE_FN_PARAM)
      ++n;
  }
  return n;
}

/* Interns a freshly-allocated native function as a chunk constant and returns
 * its index. The chunk takes ownership of the single allocation reference. */
static u8 intern_native_constant(struct oak_compiler_t* c,
                                 const oak_native_fn_t impl,
                                 const int arity_min,
                                 const int arity_max,
                                 const char* name)
{
  struct oak_obj_native_fn_t* native =
      oak_native_fn_new(impl, arity_min, arity_max, name);
  return intern_constant(c, OAK_VALUE_OBJ(&native->obj));
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

  const u8 idx = intern_native_constant(c,
                                        binding->impl,
                                        binding->arity_min,
                                        binding->arity_max,
                                        binding->name);

  struct oak_registered_fn_t* slot = &c->fn_registry[c->fn_registry_count++];
  slot->name = binding->name;
  slot->name_len = strlen(binding->name);
  slot->const_idx = idx;
  slot->arity_min = binding->arity_min;
  slot->arity_max = binding->arity_max;
  slot->decl = null;
}

static const struct oak_native_binding_t native_builtins[] = {
  { "print", oak_builtin_print, 1, 1 },
  { "input", oak_builtin_input, 0, 1 },
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

static void validate_array_push_args(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* call,
                                     struct oak_type_t recv_ty,
                                     const struct oak_token_t* err_tok);

struct oak_array_method_def_t
{
  const char* name;
  oak_native_fn_t impl;
  /* Total arity, including the implicit receiver. */
  int total_arity;
  oak_type_id_t return_type_id;
  oak_method_validate_args_fn validate_args;
};

static void validate_map_key_arg(struct oak_compiler_t* c,
                                 const struct oak_ast_node_t* call,
                                 struct oak_type_t recv_ty,
                                 const struct oak_token_t* err_tok);

static const struct oak_array_method_def_t array_method_table[] = {
  /* push(receiver, value) -> new length. */
  { "push", oak_builtin_push, 2, OAK_TYPE_NUMBER, validate_array_push_args },
  /* len(receiver) -> length. */
  { "len", oak_builtin_len, 1, OAK_TYPE_NUMBER, null },
};

static const struct oak_array_method_def_t map_method_table[] = {
  /* len(receiver) -> length. */
  { "len", oak_builtin_len, 1, OAK_TYPE_NUMBER, null },
  /* has(receiver, key) -> bool. */
  { "has", oak_builtin_has, 2, OAK_TYPE_BOOL, validate_map_key_arg },
  /* delete(receiver, key) -> bool (true if removed). */
  { "delete", oak_builtin_delete, 2, OAK_TYPE_BOOL, validate_map_key_arg },
};

static void register_array_methods(struct oak_compiler_t* c)
{
  for (usize i = 0; i < oak_count_of(array_method_table); ++i)
  {
    if (c->array_method_count >= OAK_MAX_ARRAY_METHODS)
    {
      compiler_error_at(
          c, null, "too many array methods (max %d)", OAK_MAX_ARRAY_METHODS);
      return;
    }

    const struct oak_array_method_def_t* def = &array_method_table[i];
    const u8 idx = intern_native_constant(
        c, def->impl, def->total_arity, def->total_arity, def->name);
    if (c->has_error)
      return;

    struct oak_method_binding_t* slot =
        &c->array_methods[c->array_method_count++];
    slot->name = def->name;
    slot->name_len = strlen(def->name);
    slot->const_idx = idx;
    slot->total_arity = def->total_arity;
    slot->return_type_id = def->return_type_id;
    slot->validate_args = def->validate_args;
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

static void register_map_methods(struct oak_compiler_t* c)
{
  for (usize i = 0; i < oak_count_of(map_method_table); ++i)
  {
    if (c->map_method_count >= OAK_MAX_MAP_METHODS)
    {
      compiler_error_at(
          c, null, "too many map methods (max %d)", OAK_MAX_MAP_METHODS);
      return;
    }

    const struct oak_array_method_def_t* def = &map_method_table[i];
    const u8 idx = intern_native_constant(
        c, def->impl, def->total_arity, def->total_arity, def->name);
    if (c->has_error)
      return;

    struct oak_method_binding_t* slot = &c->map_methods[c->map_method_count++];
    slot->name = def->name;
    slot->name_len = strlen(def->name);
    slot->const_idx = idx;
    slot->total_arity = def->total_arity;
    slot->return_type_id = def->return_type_id;
    slot->validate_args = def->validate_args;
  }
}

static const struct oak_method_binding_t*
find_map_method(struct oak_compiler_t* c, const char* name, const usize len)
{
  for (int i = 0; i < c->map_method_count; ++i)
  {
    const struct oak_method_binding_t* m = &c->map_methods[i];
    if (m->name_len == len && memcmp(m->name, name, len) == 0)
      return m;
  }
  return null;
}

static const struct oak_registered_struct_t*
find_struct_by_name(const struct oak_compiler_t* c,
                    const char* name,
                    const usize len)
{
  for (int i = 0; i < c->struct_count; ++i)
  {
    const struct oak_registered_struct_t* s = &c->structs[i];
    if (s->name_len == len && memcmp(s->name, name, len) == 0)
      return s;
  }
  return null;
}

static const struct oak_registered_struct_t*
find_struct_by_type_id(const struct oak_compiler_t* c,
                       const oak_type_id_t type_id)
{
  if (type_id == OAK_TYPE_UNKNOWN)
    return null;
  for (int i = 0; i < c->struct_count; ++i)
  {
    if (c->structs[i].type_id == type_id)
      return &c->structs[i];
  }
  return null;
}

static int find_struct_field(const struct oak_registered_struct_t* s,
                             const char* name,
                             const usize len)
{
  for (int i = 0; i < s->field_count; ++i)
  {
    const struct oak_struct_field_t* f = &s->fields[i];
    if (f->name_len == len && memcmp(f->name, name, len) == 0)
      return i;
  }
  return -1;
}

/* Walk all top-level struct declarations and register each in the compiler's
 * struct registry. The struct's type id is interned into the type registry so
 * later passes (function param types, struct literals) can resolve them. */
static void register_program_structs(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_STRUCT_DECL)
      continue;

    const struct oak_list_entry_t* first = item->children.next;
    if (first == &item->children)
    {
      compiler_error_at(c, item->token, "malformed struct declaration");
      return;
    }

    /* The grammar produces TYPE_NAME first; for a plain user struct it nests
     * an IDENT child. We only support simple ident names for struct types. */
    const struct oak_ast_node_t* type_name_node =
        oak_container_of(first, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* name_ident = type_name_node;
    if (type_name_node->kind == OAK_NODE_TYPE_NAME)
    {
      const struct oak_list_entry_t* tn_first = type_name_node->children.next;
      if (tn_first == &type_name_node->children)
      {
        compiler_error_at(
            c, item->token, "struct type name must be an identifier");
        return;
      }
      name_ident = oak_container_of(tn_first, struct oak_ast_node_t, link);
    }
    if (name_ident->kind != OAK_NODE_IDENT)
    {
      compiler_error_at(
          c, item->token, "struct type name must be an identifier");
      return;
    }

    const char* name = oak_token_text(name_ident->token);
    const int name_len = oak_token_length(name_ident->token);

    if (find_struct_by_name(c, name, (usize)name_len))
    {
      compiler_error_at(
          c, name_ident->token, "duplicate struct '%.*s'", name_len, name);
      return;
    }

    if (c->struct_count >= OAK_MAX_STRUCTS)
    {
      compiler_error_at(
          c, item->token, "too many structs (max %d)", OAK_MAX_STRUCTS);
      return;
    }

    struct oak_registered_struct_t* slot = &c->structs[c->struct_count++];
    slot->name = name;
    slot->name_len = (usize)name_len;
    slot->type_id =
        oak_type_registry_intern(&c->type_registry, name, (usize)name_len);
    if (slot->type_id == OAK_TYPE_UNKNOWN)
    {
      compiler_error_at(
          c, name_ident->token, "type registry full while declaring struct");
      return;
    }
    slot->field_count = 0;

    /* Collect field declarations in source order. Each entry is a binary node
     * STRUCT_FIELD_DECL(IDENT, IDENT) where lhs is the field name and rhs
     * names the field's type. */
    for (struct oak_list_entry_t* fpos = first->next; fpos != &item->children;
         fpos = fpos->next)
    {
      const struct oak_ast_node_t* fdecl =
          oak_container_of(fpos, struct oak_ast_node_t, link);
      if (fdecl->kind != OAK_NODE_STRUCT_FIELD_DECL || !fdecl->lhs ||
          !fdecl->rhs)
      {
        compiler_error_at(c, item->token, "malformed struct field");
        return;
      }
      if (slot->field_count >= OAK_MAX_STRUCT_FIELDS)
      {
        compiler_error_at(c,
                          fdecl->lhs->token,
                          "too many fields in struct '%.*s' (max %d)",
                          name_len,
                          name,
                          OAK_MAX_STRUCT_FIELDS);
        return;
      }

      const struct oak_ast_node_t* fname = fdecl->lhs;
      const struct oak_ast_node_t* ftype = fdecl->rhs;
      if (fname->kind != OAK_NODE_IDENT || ftype->kind != OAK_NODE_IDENT)
      {
        compiler_error_at(
            c, fdecl->lhs->token, "struct field must be 'name : type'");
        return;
      }

      const char* fn_name = oak_token_text(fname->token);
      const usize fn_len = (usize)oak_token_length(fname->token);
      for (int i = 0; i < slot->field_count; ++i)
      {
        if (slot->fields[i].name_len == fn_len &&
            memcmp(slot->fields[i].name, fn_name, fn_len) == 0)
        {
          compiler_error_at(c,
                            fname->token,
                            "duplicate field '%.*s' in struct '%.*s'",
                            (int)fn_len,
                            fn_name,
                            name_len,
                            name);
          return;
        }
      }

      struct oak_struct_field_t* f = &slot->fields[slot->field_count++];
      f->name = fn_name;
      f->name_len = fn_len;
      oak_type_clear(&f->type);
      f->type.id = intern_type_token(c, ftype->token);
    }
  }
}

static void register_program_functions(struct oak_compiler_t* c,
                                       const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_FN_DECL)
      continue;

    const struct oak_ast_node_t* name_node = fn_decl_name_node(item);
    const char* name = oak_token_text(name_node->token);
    const int name_len = oak_token_length(name_node->token);
    const int explicit_arity = count_fn_params(item);

    const struct oak_ast_node_t* self_param = fn_decl_self_param(item);

    if (fn_decl_has_receiver(item))
    {
      const struct oak_ast_node_t* recv_node =
          oak_container_of(item->children.next, struct oak_ast_node_t, link);
      const struct oak_ast_node_t* recv_ident = recv_node->child;
      if (!recv_ident || recv_ident->kind != OAK_NODE_IDENT)
      {
        compiler_error_at(
            c, recv_node->token, "method receiver must be a type name");
        return;
      }

      if (!self_param)
      {
        compiler_error_at(
            c,
            name_node->token,
            "method '%.*s' must declare 'self' or 'mut self' as its first"
            " parameter",
            name_len,
            name);
        return;
      }

      const char* sname = oak_token_text(recv_ident->token);
      const int sname_len = oak_token_length(recv_ident->token);
      struct oak_registered_struct_t* sd = null;
      for (int i = 0; i < c->struct_count; ++i)
      {
        struct oak_registered_struct_t* cand = &c->structs[i];
        if (cand->name_len == (usize)sname_len &&
            memcmp(cand->name, sname, (usize)sname_len) == 0)
        {
          sd = cand;
          break;
        }
      }
      if (!sd)
      {
        compiler_error_at(c,
                          recv_ident->token,
                          "no such struct '%.*s' for method receiver",
                          sname_len,
                          sname);
        return;
      }

      for (int i = 0; i < sd->method_count; ++i)
      {
        const struct oak_struct_method_t* e = &sd->methods[i];
        if (e->name_len == (usize)name_len &&
            memcmp(e->name, name, (usize)name_len) == 0)
        {
          compiler_error_at(c,
                            name_node->token,
                            "duplicate method '%.*s' on struct '%.*s'",
                            name_len,
                            name,
                            (int)sd->name_len,
                            sd->name);
          return;
        }
      }

      if (sd->method_count >= OAK_MAX_STRUCT_METHODS)
      {
        compiler_error_at(c,
                          name_node->token,
                          "too many methods on struct '%.*s' (max %d)",
                          (int)sd->name_len,
                          sd->name,
                          OAK_MAX_STRUCT_METHODS);
        return;
      }

      const int total_arity = explicit_arity + 1;
      struct oak_obj_fn_t* fn_obj = oak_fn_new(0, total_arity);
      const u8 idx = intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

      struct oak_struct_method_t* slot = &sd->methods[sd->method_count++];
      slot->name = name;
      slot->name_len = (usize)name_len;
      slot->const_idx = idx;
      slot->arity = total_arity;
      slot->decl = item;
      continue;
    }

    if (self_param)
    {
      const struct oak_ast_node_t* first_child = oak_container_of(
          self_param->children.next, struct oak_ast_node_t, link);
      compiler_error_at(c,
                        first_child->token,
                        "'self' parameter is only valid on methods (use"
                        " 'fn TypeName.%.*s(self, ...)' instead)",
                        name_len,
                        name);
      return;
    }

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

    struct oak_obj_fn_t* fn_obj = oak_fn_new(0, explicit_arity);
    const u8 idx = intern_constant(c, OAK_VALUE_OBJ(&fn_obj->obj));

    struct oak_registered_fn_t* slot = &c->fn_registry[c->fn_registry_count++];
    slot->name = name;
    slot->name_len = (usize)name_len;
    slot->const_idx = idx;
    slot->arity_min = explicit_arity;
    slot->arity_max = explicit_arity;
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
  *out_arity = e->arity_max;
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

/* If `recv_struct` is non-null, the function is treated as a method: an
 * implicit `self` local is installed at slot 0 with the receiver's static
 * type, and explicit parameters start at slot 1. */
static void compile_function_body(struct oak_compiler_t* c,
                                  const struct oak_ast_node_t* decl,
                                  const struct oak_registered_struct_t* recv)
{
  const struct oak_ast_node_t* body = fn_decl_block(decl);
  if (!body || body->kind != OAK_NODE_BLOCK)
  {
    compiler_error_at(c, decl->token, "function has no body");
    return;
  }

  c->function_depth++;
  c->local_count = 0;
  c->scope_depth = 0;
  c->stack_depth = 0;
  c->current_loop = null;

  int slot = 0;
  if (recv)
  {
    const struct oak_ast_node_t* sp = fn_decl_self_param(decl);
    /* The presence of FN_PARAM_SELF was already checked when the method
     * was registered; treat its absence here as an internal error. */
    oak_assert(sp != null);
    struct oak_type_t self_ty;
    oak_type_clear(&self_ty);
    self_ty.id = recv->type_id;
    add_local(c, "self", 4u, slot++, fn_param_self_is_mutable(sp), self_ty);
  }

  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &decl->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind != OAK_NODE_FN_PARAM)
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
    if (type_id && type_id->kind == OAK_NODE_IDENT)
      param_type.id = intern_type_token(c, type_id->token);
    add_local(c,
              oak_token_text(id->token),
              (usize)oak_token_length(id->token),
              slot++,
              fn_param_is_mutable(ch),
              param_type);
  }

  c->stack_depth = slot;

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
    compile_function_body(c, e->decl, null);
    if (c->has_error)
      return;
  }

  for (int s = 0; s < c->struct_count; ++s)
  {
    const struct oak_registered_struct_t* sd = &c->structs[s];
    for (int m = 0; m < sd->method_count; ++m)
    {
      const struct oak_struct_method_t* me = &sd->methods[m];
      if (!me->decl)
        continue;
      struct oak_value_t fn_val = c->chunk->constants[me->const_idx];
      struct oak_obj_fn_t* fn_obj = oak_as_fn(fn_val);
      fn_obj->code_offset = c->chunk->count;
      compile_function_body(c, me->decl, sd);
      if (c->has_error)
        return;
    }
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
    if (item->kind == OAK_NODE_FN_DECL)
      continue;
    /* Struct declarations are processed in a pre-pass; they don't emit code. */
    if (item->kind == OAK_NODE_STRUCT_DECL)
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
  register_map_methods(c);
  if (c->has_error)
    return;
  /* Structs must be registered before functions so that function parameter
   * types can refer to user-defined structs. */
  register_program_structs(c, program);
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
  if (oak_node_is_unary_op(node->kind))
    return node->child ? 1u : 0u;
  if (oak_node_is_binary_op(node->kind))
    return (usize)(node->lhs ? 1 : 0) + (usize)(node->rhs ? 1 : 0);
  return oak_list_length(&node->children);
}

static int ast_is_int_literal(const struct oak_ast_node_t* node,
                              const int value)
{
  return node && node->kind == OAK_NODE_INT &&
         oak_token_as_i32(node->token) == value;
}

static u8 opcode_for_node_kind(const enum oak_node_kind_t kind)
{
  switch (kind)
  {
    case OAK_NODE_BINARY_ADD:
    case OAK_NODE_STMT_ADD_ASSIGN:
      return OAK_OP_ADD;
    case OAK_NODE_BINARY_SUB:
    case OAK_NODE_STMT_SUB_ASSIGN:
      return OAK_OP_SUB;
    case OAK_NODE_BINARY_MUL:
    case OAK_NODE_STMT_MUL_ASSIGN:
      return OAK_OP_MUL;
    case OAK_NODE_BINARY_DIV:
    case OAK_NODE_STMT_DIV_ASSIGN:
      return OAK_OP_DIV;
    case OAK_NODE_BINARY_MOD:
    case OAK_NODE_STMT_MOD_ASSIGN:
      return OAK_OP_MOD;
    case OAK_NODE_BINARY_EQ:
      return OAK_OP_EQ;
    case OAK_NODE_BINARY_NEQ:
      return OAK_OP_NEQ;
    case OAK_NODE_BINARY_LESS:
      return OAK_OP_LT;
    case OAK_NODE_BINARY_LESS_EQ:
      return OAK_OP_LE;
    case OAK_NODE_BINARY_GREATER:
      return OAK_OP_GT;
    case OAK_NODE_BINARY_GREATER_EQ:
      return OAK_OP_GE;
    case OAK_NODE_UNARY_NEG:
      return OAK_OP_NEGATE;
    case OAK_NODE_UNARY_NOT:
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
    case OAK_NODE_INT:
    case OAK_NODE_FLOAT:
    case OAK_NODE_UNARY_NEG:
    case OAK_NODE_BINARY_ADD:
    case OAK_NODE_BINARY_SUB:
    case OAK_NODE_BINARY_MUL:
    case OAK_NODE_BINARY_DIV:
    case OAK_NODE_BINARY_MOD:
      out->id = OAK_TYPE_NUMBER;
      return;
    case OAK_NODE_STRING:
      out->id = OAK_TYPE_STRING;
      return;
    case OAK_NODE_UNARY_NOT:
    case OAK_NODE_BINARY_EQ:
    case OAK_NODE_BINARY_NEQ:
    case OAK_NODE_BINARY_LESS:
    case OAK_NODE_BINARY_LESS_EQ:
    case OAK_NODE_BINARY_GREATER:
    case OAK_NODE_BINARY_GREATER_EQ:
      out->id = OAK_TYPE_BOOL;
      return;
    case OAK_NODE_IDENT:
    {
      const char* name = oak_token_text(expr->token);
      const usize len = (usize)oak_token_length(expr->token);
      struct oak_type_t local_ty;
      oak_type_clear(&local_ty);
      if (local_type_get(c, name, len, &local_ty))
        *out = local_ty;
      return;
    }
    case OAK_NODE_SELF:
    {
      struct oak_type_t local_ty;
      oak_type_clear(&local_ty);
      if (local_type_get(c, "self", 4u, &local_ty))
        *out = local_ty;
      return;
    }
    case OAK_NODE_FN_CALL:
    {
      const struct oak_list_entry_t* first = expr->children.next;
      if (first == &expr->children)
        return;
      const struct oak_ast_node_t* callee =
          oak_container_of(first, struct oak_ast_node_t, link);
      if (!callee)
        return;
      if (callee->kind == OAK_NODE_MEMBER_ACCESS)
      {
        const struct oak_ast_node_t* recv = callee->lhs;
        const struct oak_ast_node_t* method = callee->rhs;
        if (!recv || !method || method->kind != OAK_NODE_IDENT)
          return;
        struct oak_type_t recv_ty;
        infer_expr_static_type(c, recv, &recv_ty);
        const char* mn = oak_token_text(method->token);
        const usize mn_len = (usize)oak_token_length(method->token);
        if (oak_type_is_known(&recv_ty) && !recv_ty.is_array &&
            !recv_ty.is_map)
        {
          const struct oak_registered_struct_t* sd =
              find_struct_by_type_id(c, recv_ty.id);
          if (sd)
          {
            for (int i = 0; i < sd->method_count; ++i)
            {
              const struct oak_struct_method_t* sm = &sd->methods[i];
              if (sm->name_len == mn_len &&
                  memcmp(sm->name, mn, mn_len) == 0)
              {
                if (sm->decl)
                {
                  const struct oak_ast_node_t* ret =
                      fn_decl_return_type_node(sm->decl);
                  if (ret && ret->kind == OAK_NODE_IDENT)
                    out->id = intern_type_token(c, ret->token);
                }
                return;
              }
            }
          }
          return;
        }
        const struct oak_method_binding_t* m = null;
        if (recv_ty.is_array)
          m = find_array_method(c, mn, mn_len);
        else if (recv_ty.is_map)
          m = find_map_method(c, mn, mn_len);
        if (m)
          out->id = m->return_type_id;
        return;
      }
      if (callee->kind != OAK_NODE_IDENT)
        return;
      const char* cn = oak_token_text(callee->token);
      const usize clen = (usize)oak_token_length(callee->token);
      const struct oak_registered_fn_t* fe =
          find_registered_fn_entry(c, cn, clen);
      if (fe && !fe->decl && fe->name_len == 5u &&
          memcmp(fe->name, "input", 5u) == 0)
      {
        out->id = OAK_TYPE_STRING;
        return;
      }
      if (!fe || !fe->decl)
        return;
      const struct oak_ast_node_t* ret = fn_decl_return_type_node(fe->decl);
      if (ret && ret->kind == OAK_NODE_IDENT)
        out->id = intern_type_token(c, ret->token);
      return;
    }
    case OAK_NODE_EXPR_CAST:
    {
      const struct oak_ast_node_t* type_node = expr->rhs;
      if (!type_node)
        return;
      if (type_node->kind == OAK_NODE_TYPE_ARRAY)
      {
        const struct oak_ast_node_t* elem = type_node->child;
        if (!elem || elem->kind != OAK_NODE_IDENT)
          return;
        out->id = intern_type_token(c, elem->token);
        out->is_array = 1;
        return;
      }
      if (type_node->kind == OAK_NODE_TYPE_MAP)
      {
        const struct oak_ast_node_t* key = type_node->lhs;
        const struct oak_ast_node_t* val = type_node->rhs;
        if (!key || !val || key->kind != OAK_NODE_IDENT ||
            val->kind != OAK_NODE_IDENT)
          return;
        out->key_id = intern_type_token(c, key->token);
        out->id = intern_type_token(c, val->token);
        out->is_map = 1;
        return;
      }
      if (type_node->kind == OAK_NODE_IDENT)
        out->id = intern_type_token(c, type_node->token);
      return;
    }
    case OAK_NODE_EXPR_ARRAY_LITERAL:
    {
      const struct oak_list_entry_t* first = expr->children.next;
      if (first == &expr->children)
        return;
      const struct oak_ast_node_t* first_wrap =
          oak_container_of(first, struct oak_ast_node_t, link);
      const struct oak_ast_node_t* first_elem =
          first_wrap->kind == OAK_NODE_ARRAY_LITERAL_ELEMENT ? first_wrap->child
                                                             : first_wrap;
      struct oak_type_t elem_ty;
      infer_expr_static_type(c, first_elem, &elem_ty);
      if (!oak_type_is_known(&elem_ty))
        return;
      out->id = elem_ty.id;
      out->is_array = 1;
      return;
    }
    case OAK_NODE_EXPR_MAP_LITERAL:
    {
      const struct oak_list_entry_t* first = expr->children.next;
      if (first == &expr->children)
        return;
      const struct oak_ast_node_t* first_entry =
          oak_container_of(first, struct oak_ast_node_t, link);
      if (first_entry->kind != OAK_NODE_MAP_LITERAL_ENTRY)
        return;
      struct oak_type_t key_ty;
      struct oak_type_t val_ty;
      infer_expr_static_type(c, first_entry->lhs, &key_ty);
      infer_expr_static_type(c, first_entry->rhs, &val_ty);
      if (!oak_type_is_known(&key_ty) || !oak_type_is_known(&val_ty))
        return;
      out->key_id = key_ty.id;
      out->id = val_ty.id;
      out->is_map = 1;
      return;
    }
    case OAK_NODE_INDEX_ACCESS:
    {
      struct oak_type_t coll_ty;
      infer_expr_static_type(c, expr->lhs, &coll_ty);
      if (coll_ty.is_array && oak_type_is_known(&coll_ty))
      {
        out->id = coll_ty.id;
        return;
      }
      if (coll_ty.is_map && oak_type_is_known(&coll_ty))
      {
        out->id = coll_ty.id;
        return;
      }
      return;
    }
    case OAK_NODE_EXPR_STRUCT_LITERAL:
    {
      const struct oak_list_entry_t* first = expr->children.next;
      if (first == &expr->children)
        return;
      const struct oak_ast_node_t* name_node =
          oak_container_of(first, struct oak_ast_node_t, link);
      if (!name_node || name_node->kind != OAK_NODE_IDENT)
        return;
      const struct oak_registered_struct_t* sd =
          find_struct_by_name(c,
                              oak_token_text(name_node->token),
                              (usize)oak_token_length(name_node->token));
      if (!sd)
        return;
      out->id = sd->type_id;
      return;
    }
    case OAK_NODE_MEMBER_ACCESS:
    {
      const struct oak_ast_node_t* recv = expr->lhs;
      const struct oak_ast_node_t* fname = expr->rhs;
      if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
        return;
      struct oak_type_t recv_ty;
      infer_expr_static_type(c, recv, &recv_ty);
      if (!oak_type_is_known(&recv_ty))
        return;
      const struct oak_registered_struct_t* sd =
          find_struct_by_type_id(c, recv_ty.id);
      if (!sd)
        return;
      const int idx = find_struct_field(sd,
                                        oak_token_text(fname->token),
                                        (usize)oak_token_length(fname->token));
      if (idx < 0)
        return;
      *out = sd->fields[idx].type;
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

/* Format a type name into a thread-local buffer for error messages. */
static const char* type_full_name(struct oak_compiler_t* c,
                                  const struct oak_type_t t)
{
  static _Thread_local char buf[128];
  if (t.is_map)
  {
    snprintf(buf,
             sizeof(buf),
             "[%s:%s]",
             oak_type_registry_name(&c->type_registry, t.key_id),
             oak_type_registry_name(&c->type_registry, t.id));
    return buf;
  }
  if (t.is_array)
  {
    snprintf(buf, sizeof(buf), "%s[]", type_kind_name(c, t));
    return buf;
  }
  return type_kind_name(c, t);
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
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    const struct oak_ast_node_t* param = fn_decl_param_at(fn->decl, (int)i);
    if (!param)
    {
      compiler_error_at(c, null, "internal error: missing parameter %zu", i);
      return;
    }
    const struct oak_ast_node_t* want_type_node = fn_param_type_ident(param);
    if (!want_type_node || want_type_node->kind != OAK_NODE_IDENT)
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
      if (!err_tok && arg_wrap->kind == OAK_NODE_FN_CALL_ARG &&
          arg_wrap->child && arg_wrap->child->token)
        err_tok = arg_wrap->child->token;
      compiler_error_at(c,
                        err_tok,
                        "argument %zu: expected type '%s', found '%s'",
                        i + 1,
                        type_full_name(c, want),
                        type_full_name(c, got));
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
            oak_token_text(ident->token),
            oak_token_length(ident->token),
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
  if (arg->kind == OAK_NODE_FN_CALL_ARG)
    compile_node(c, arg->child);
  else
    compile_node(c, arg);
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
static void compile_stmt_for_in(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* node)
{
  const usize child_count = ast_child_count(node);
  if (child_count != 3 && child_count != 4)
  {
    compiler_error_at(c, null, "malformed 'for ... in' statement");
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

  const struct oak_code_loc_t loc = code_loc_from_token(first_ident->token);

  struct oak_type_t coll_ty;
  infer_expr_static_type(c, coll_expr, &coll_ty);
  if (!oak_type_is_known(&coll_ty) || (!coll_ty.is_array && !coll_ty.is_map))
  {
    compiler_error_at(c,
                      coll_expr->token ? coll_expr->token : first_ident->token,
                      "'for ... in' requires an array or map, got '%s'",
                      type_full_name(c, coll_ty));
    return;
  }

  /* Look up the receiver's len() binding so we can snapshot length once. */
  const struct oak_method_binding_t* len_m =
      coll_ty.is_map ? find_map_method(c, "len", 3)
                     : find_array_method(c, "len", 3);
  if (!len_m)
  {
    compiler_error_at(c,
                      coll_expr->token ? coll_expr->token : first_ident->token,
                      "internal error: missing 'len' method binding");
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
    if (coll_ty.is_map)
      k_ident = first_ident; /* iterate keys by default */
    else
      v_ident = first_ident; /* arrays: iterate values */
  }

  const int base_depth = c->stack_depth;

  begin_scope(c);

  /* slot 0: the collection itself (evaluated exactly once). */
  compile_node(c, coll_expr);
  const int coll_slot = c->stack_depth - 1;
  add_local(c, "$coll", 0, coll_slot, 0, coll_ty);

  /* slot 1: iteration index, mutable hidden local. */
  emit_op_arg(c, OAK_OP_CONSTANT, intern_constant(c, OAK_VALUE_I32(0)), loc);
  const int idx_slot = c->stack_depth - 1;
  const struct oak_type_t num_ty = { .id = OAK_TYPE_NUMBER };
  add_local(c, "$i", 0, idx_slot, 1, num_ty);

  /* slot 2: snapshot length. Emit `len_fn(coll)` via OP_CALL. */
  emit_op_arg(c, OAK_OP_CONSTANT, len_m->const_idx, loc);
  emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)coll_slot, loc);
  emit_op_arg(c, OAK_OP_CALL, (u8)len_m->total_arity, loc);
  c->stack_depth -= len_m->total_arity;
  const int limit_slot = c->stack_depth - 1;
  add_local(c, "$n", 0, limit_slot, 0, num_ty);

  struct oak_loop_frame_t loop = {
    .enclosing = c->current_loop,
    .loop_start = c->chunk->count,
    .exit_depth = base_depth,
    .continue_depth = base_depth + 3,
    .break_count = 0,
    .continue_count = 0,
  };
  c->current_loop = &loop;

  /* Loop condition: idx < limit. */
  emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)idx_slot, loc);
  emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)limit_slot, loc);
  emit_op(c, OAK_OP_LT, loc);
  const usize exit_jump = emit_jump(c, OAK_OP_JUMP_IF_FALSE, loc);

  /* Per-iteration scope: exposes k, v to the body. */
  begin_scope(c);

  /* Push k (key for maps, index for arrays). */
  if (k_ident)
  {
    if (coll_ty.is_map)
    {
      emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)coll_slot, loc);
      emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)idx_slot, loc);
      emit_op(c, OAK_OP_MAP_KEY_AT, loc);
      const struct oak_type_t key_ty = { .id = coll_ty.key_id };
      add_local(c,
                oak_token_text(k_ident->token),
                (usize)oak_token_length(k_ident->token),
                c->stack_depth - 1,
                0,
                key_ty);
    }
    else
    {
      emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)idx_slot, loc);
      add_local(c,
                oak_token_text(k_ident->token),
                (usize)oak_token_length(k_ident->token),
                c->stack_depth - 1,
                0,
                num_ty);
    }
  }

  /* Push v (value for both maps and arrays). */
  if (v_ident)
  {
    emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)coll_slot, loc);
    emit_op_arg(c, OAK_OP_GET_LOCAL, (u8)idx_slot, loc);
    emit_op(c, coll_ty.is_map ? OAK_OP_MAP_VALUE_AT : OAK_OP_GET_INDEX, loc);
    const struct oak_type_t val_ty = { .id = coll_ty.id };
    add_local(c,
              oak_token_text(v_ident->token),
              (usize)oak_token_length(v_ident->token),
              c->stack_depth - 1,
              0,
              val_ty);
  }

  compile_block(c, body);

  /* Pop per-iter k, v (compile-time + runtime). */
  end_scope(c);

  /* `continue` lands here (after k/v are popped). */
  patch_jumps(c, loop.continue_jumps, loop.continue_count);

  emit_op_arg(c, OAK_OP_INC_LOCAL, (u8)idx_slot, loc);
  emit_loop(c, loop.loop_start, loc);
  patch_jump(c, exit_jump);

  /* Tear down hidden iterator state ($n, $i, $coll). */
  end_scope(c);

  /* `break` lands here, after all iterator state is popped. */
  patch_jumps(c, loop.break_jumps, loop.break_count);

  c->current_loop = loop.enclosing;
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
    if (arg->kind == OAK_NODE_FN_CALL_ARG)
      return arg->child;
    return arg;
  }
  return null;
}

static void validate_array_push_args(struct oak_compiler_t* c,
                                     const struct oak_ast_node_t* call,
                                     struct oak_type_t recv_ty,
                                     const struct oak_token_t* err_tok)
{
  const struct oak_ast_node_t* val_expr = fn_call_arg_expr_at(call, 0);
  if (!val_expr)
    return;

  struct oak_type_t val_ty;
  infer_expr_static_type(c, val_expr, &val_ty);
  if (!oak_type_is_known(&val_ty))
    return;

  const struct oak_type_t element_ty = { .id = recv_ty.id };
  if (!oak_type_equal(&element_ty, &val_ty))
  {
    compiler_error_at(c,
                      val_expr->token ? val_expr->token : err_tok,
                      "cannot push value of type '%s' to array of '%s'",
                      type_full_name(c, val_ty),
                      type_full_name(c, element_ty));
  }
}

static void validate_map_key_arg(struct oak_compiler_t* c,
                                 const struct oak_ast_node_t* call,
                                 const struct oak_type_t recv_ty,
                                 const struct oak_token_t* err_tok)
{
  const struct oak_ast_node_t* key_expr = fn_call_arg_expr_at(call, 0);
  if (!key_expr)
    return;

  struct oak_type_t key_ty;
  infer_expr_static_type(c, key_expr, &key_ty);
  if (!oak_type_is_known(&key_ty))
    return;

  const struct oak_type_t want_key = { .id = recv_ty.key_id };
  if (!oak_type_equal(&want_key, &key_ty))
  {
    compiler_error_at(c,
                      key_expr->token ? key_expr->token : err_tok,
                      "map key must be of type '%s', got '%s'",
                      type_full_name(c, want_key),
                      type_full_name(c, key_ty));
  }
}

/* Type-check explicit arguments of a struct method call against the method's
 * declared parameters. The receiver itself is not validated here (it has
 * already been checked to be the right struct type by the caller). */
static void
validate_struct_method_call_arg_types(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* call,
                                      const struct oak_struct_method_t* m)
{
  if (!m->decl)
    return;
  const struct oak_list_entry_t* first = call->children.next;
  struct oak_list_entry_t* pos = first->next;
  int i = 0;
  for (; pos != &call->children; pos = pos->next, ++i)
  {
    const struct oak_ast_node_t* arg_wrap =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* arg_expr = arg_wrap;
    if (arg_wrap->kind == OAK_NODE_FN_CALL_ARG)
      arg_expr = arg_wrap->child;

    const struct oak_ast_node_t* param = fn_decl_param_at(m->decl, i);
    if (!param)
    {
      compiler_error_at(c, null, "internal error: missing parameter %d", i);
      return;
    }
    const struct oak_ast_node_t* want_type_node = fn_param_type_ident(param);
    if (!want_type_node || want_type_node->kind != OAK_NODE_IDENT)
    {
      compiler_error_at(
          c, param->token, "malformed function parameter (expected type name)");
      return;
    }
    const struct oak_type_t want = {
      .id = intern_type_token(c, want_type_node->token),
    };

    struct oak_type_t got;
    infer_expr_static_type(c, arg_expr, &got);
    if (!oak_type_is_known(&got))
      continue;
    if (!oak_type_equal(&want, &got))
    {
      const struct oak_token_t* err_tok = arg_expr->token;
      if (!err_tok && arg_wrap->kind == OAK_NODE_FN_CALL_ARG &&
          arg_wrap->child && arg_wrap->child->token)
        err_tok = arg_wrap->child->token;
      compiler_error_at(c,
                        err_tok,
                        "argument %d: expected type '%s', found '%s'",
                        i + 1,
                        type_full_name(c, want),
                        type_full_name(c, got));
    }
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
  if (!receiver || !method || method->kind != OAK_NODE_IDENT)
  {
    compiler_error_at(
        c, callee->token, "method call requires 'receiver.name(...)' form");
    return;
  }

  const struct oak_code_loc_t call_loc = code_loc_from_token(method->token);
  const usize user_argc = ast_child_count(node) - 1;
  const char* mname = oak_token_text(method->token);
  const int mname_len = oak_token_length(method->token);

  struct oak_type_t recv_ty;
  infer_expr_static_type(c, receiver, &recv_ty);

  /* Struct method calls dispatch to a regular user function whose first
   * parameter is the receiver (`self`). */
  if (oak_type_is_known(&recv_ty) && !recv_ty.is_array && !recv_ty.is_map)
  {
    const struct oak_registered_struct_t* sd =
        find_struct_by_type_id(c, recv_ty.id);
    if (sd)
    {
      const struct oak_struct_method_t* sm = null;
      for (int i = 0; i < sd->method_count; ++i)
      {
        const struct oak_struct_method_t* cand = &sd->methods[i];
        if (cand->name_len == (usize)mname_len &&
            memcmp(cand->name, mname, (usize)mname_len) == 0)
        {
          sm = cand;
          break;
        }
      }
      if (!sm)
      {
        compiler_error_at(c,
                          method->token,
                          "no method '%.*s' on struct '%.*s'",
                          mname_len,
                          mname,
                          (int)sd->name_len,
                          sd->name);
        return;
      }
      const int expected_user = sm->arity - 1;
      if ((int)user_argc != expected_user)
      {
        compiler_error_at(c,
                          method->token,
                          "method '%.*s' expects %d arguments, got %zu",
                          mname_len,
                          mname,
                          expected_user,
                          user_argc);
        return;
      }

      validate_struct_method_call_arg_types(c, node, sm);
      if (c->has_error)
        return;

      emit_op_arg(c, OAK_OP_CONSTANT, sm->const_idx, call_loc);
      compile_node(c, receiver);

      const struct oak_list_entry_t* first = node->children.next;
      for (struct oak_list_entry_t* pos = first->next; pos != &node->children;
           pos = pos->next)
      {
        const struct oak_ast_node_t* arg =
            oak_container_of(pos, struct oak_ast_node_t, link);
        compile_fn_call_arg(c, arg);
      }

      emit_op_arg(c, OAK_OP_CALL, (u8)sm->arity, call_loc);
      c->stack_depth -= sm->arity;
      return;
    }
  }

  if ((!recv_ty.is_array && !recv_ty.is_map) || !oak_type_is_known(&recv_ty))
  {
    compiler_error_at(c,
                      receiver->token ? receiver->token : method->token,
                      "method '.%.*s' requires a typed array, map, or struct"
                      " receiver",
                      mname_len,
                      mname);
    return;
  }

  const struct oak_method_binding_t* m =
      recv_ty.is_map ? find_map_method(c, mname, (usize)mname_len)
                     : find_array_method(c, mname, (usize)mname_len);
  if (!m)
  {
    compiler_error_at(c,
                      method->token,
                      "no method '%.*s' on %s '%s'",
                      mname_len,
                      mname,
                      recv_ty.is_map ? "map" : "array of",
                      type_full_name(c, recv_ty));
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

  if (m->validate_args)
  {
    m->validate_args(c, node, recv_ty, method->token);
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

  if (callee && callee->kind == OAK_NODE_MEMBER_ACCESS)
  {
    compile_method_call(c, node, callee);
    return;
  }

  if (!callee || callee->kind != OAK_NODE_IDENT)
  {
    compiler_error_at(
        c, callee ? callee->token : null, "callee must be an identifier");
    return;
  }

  const struct oak_code_loc_t call_loc = code_loc_from_token(callee->token);
  const usize argc = ast_child_count(node) - 1;

  const struct oak_registered_fn_t* entry = find_registered_fn_entry(
      c, oak_token_text(callee->token), (usize)oak_token_length(callee->token));
  if (!entry)
  {
    compiler_error_at(c,
                      callee->token,
                      "undefined function '%.*s'",
                      oak_token_length(callee->token),
                      oak_token_text(callee->token));
    return;
  }

  if ((int)argc < entry->arity_min || (int)argc > entry->arity_max)
  {
    if (entry->arity_min == entry->arity_max)
    {
      compiler_error_at(c,
                        callee->token,
                        "function '%.*s' expects %d arguments, got %zu",
                        oak_token_length(callee->token),
                        oak_token_text(callee->token),
                        entry->arity_min,
                        argc);
    }
    else
    {
      compiler_error_at(c,
                        callee->token,
                        "function '%.*s' expects %d to %d arguments, got %zu",
                        oak_token_length(callee->token),
                        oak_token_text(callee->token),
                        entry->arity_min,
                        entry->arity_max,
                        argc);
    }
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
    case OAK_NODE_PROGRAM:
      compiler_error_at(
          c, null, "internal error: nested program in compilation");
      break;
    case OAK_NODE_FN_DECL:
      break;
    case OAK_NODE_STMT_RETURN:
      compile_stmt_return(c, node);
      break;
    case OAK_NODE_INT:
    {
      const int value = oak_token_as_i32(node->token);
      const u8 idx = intern_constant(c, OAK_VALUE_I32(value));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_FLOAT:
    {
      const float value = oak_token_as_f32(node->token);
      const u8 idx = intern_constant(c, OAK_VALUE_F32(value));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_STRING:
    {
      const char* chars = oak_token_text(node->token);
      const int len = oak_token_length(node->token);
      struct oak_obj_string_t* str = oak_string_new(chars, (usize)len);
      const u8 idx = intern_constant(c, OAK_VALUE_OBJ(str));
      emit_op_arg(c, OAK_OP_CONSTANT, idx, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_IDENT:
    {
      const char* name = oak_token_text(node->token);
      const int len = oak_token_length(node->token);
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
    case OAK_NODE_SELF:
    {
      const int slot = find_local(c, "self", 4u, null);
      if (slot < 0)
      {
        compiler_error_at(
            c, node->token, "'self' is only valid inside a method body");
        return;
      }
      emit_op_arg(
          c, OAK_OP_GET_LOCAL, (u8)slot, code_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_BINARY_ADD:
    case OAK_NODE_BINARY_SUB:
    case OAK_NODE_BINARY_MUL:
    case OAK_NODE_BINARY_DIV:
    case OAK_NODE_BINARY_MOD:
    case OAK_NODE_BINARY_EQ:
    case OAK_NODE_BINARY_NEQ:
    case OAK_NODE_BINARY_LESS:
    case OAK_NODE_BINARY_LESS_EQ:
    case OAK_NODE_BINARY_GREATER:
    case OAK_NODE_BINARY_GREATER_EQ:
    case OAK_NODE_BINARY_AND:
    case OAK_NODE_BINARY_OR:
    {
      if (node->kind == OAK_NODE_BINARY_AND ||
          node->kind == OAK_NODE_BINARY_OR)
      {
        // TODO: short-circuit evaluation; for now, fall through to truthiness
        compiler_error_at(c,
                          null,
                          "'%s' operator not yet implemented",
                          node->kind == OAK_NODE_BINARY_AND ? "&&" : "||");
        return;
      }

      compile_node(c, node->lhs);
      compile_node(c, node->rhs);
      emit_op(c,
              opcode_for_node_kind(node->kind),
              code_loc_from_token(node->lhs->token));
      break;
    }
    case OAK_NODE_UNARY_NEG:
    case OAK_NODE_UNARY_NOT:
    {
      compile_node(c, node->child);
      emit_op(c,
              opcode_for_node_kind(node->kind),
              code_loc_from_token(node->child->token));
      break;
    }
    case OAK_NODE_STMT_EXPR:
    {
      const int depth_before = c->stack_depth;
      struct oak_ast_node_t* expr;
      oak_ast_node_unpack(node, &expr);
      compile_node(c, expr);
      if (c->stack_depth > depth_before)
        emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_STMT_LET_ASSIGNMENT:
    {
      int is_mutable = 0;
      const struct oak_ast_node_t* assign = null;

      struct oak_list_entry_t* pos;
      oak_list_for_each(pos, &node->children)
      {
        const struct oak_ast_node_t* child =
            oak_container_of(pos, struct oak_ast_node_t, link);
        if (child->kind == OAK_NODE_MUT_KEYWORD)
          is_mutable = 1;
        else if (child->kind == OAK_NODE_STMT_ASSIGNMENT)
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
      const char* name = oak_token_text(ident->token);
      const int size = oak_token_length(ident->token);
      add_local(
          c, name, (usize)size, c->stack_depth - 1, is_mutable, rhs_ty);

      break;
    }
    case OAK_NODE_STMT_ASSIGNMENT:
    {
      const struct oak_ast_node_t* lhs = node->lhs;
      const struct oak_ast_node_t* rhs = node->rhs;

      if (lhs->kind == OAK_NODE_INDEX_ACCESS)
      {
        struct oak_type_t coll_ty;
        infer_expr_static_type(c, lhs->lhs, &coll_ty);
        if ((!coll_ty.is_array && !coll_ty.is_map) ||
            !oak_type_is_known(&coll_ty))
        {
          compiler_error_at(c,
                            lhs->lhs->token,
                            "indexed assignment requires a typed array or "
                            "map");
          return;
        }

        if (coll_ty.is_map)
        {
          struct oak_type_t key_ty;
          infer_expr_static_type(c, lhs->rhs, &key_ty);
          if (oak_type_is_known(&key_ty))
          {
            const struct oak_type_t want_key = { .id = coll_ty.key_id };
            if (!oak_type_equal(&want_key, &key_ty))
            {
              compiler_error_at(c,
                                lhs->rhs->token,
                                "map key must be of type '%s', got '%s'",
                                type_full_name(c, want_key),
                                type_full_name(c, key_ty));
              return;
            }
          }
        }

        struct oak_type_t val_ty;
        infer_expr_static_type(c, rhs, &val_ty);
        if (oak_type_is_known(&val_ty))
        {
          const struct oak_type_t element_ty = { .id = coll_ty.id };
          if (!oak_type_equal(&element_ty, &val_ty))
          {
            compiler_error_at(
                c,
                rhs->token,
                "cannot assign value of type '%s' to element of '%s' %s",
                type_full_name(c, val_ty),
                type_full_name(c, element_ty),
                coll_ty.is_map ? "map" : "array");
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

      if (lhs->kind == OAK_NODE_MEMBER_ACCESS)
      {
        const struct oak_ast_node_t* recv = lhs->lhs;
        const struct oak_ast_node_t* fname = lhs->rhs;
        if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
        {
          compiler_error_at(
              c, lhs->token, "field assignment requires 'expr.field = expr'");
          return;
        }
        struct oak_type_t recv_ty;
        infer_expr_static_type(c, recv, &recv_ty);
        const struct oak_registered_struct_t* sd =
            oak_type_is_known(&recv_ty)
                ? find_struct_by_type_id(c, recv_ty.id)
                : null;
        if (!sd)
        {
          compiler_error_at(c,
                            fname->token,
                            "field assignment '.%.*s ='"
                            " requires a struct receiver",
                            oak_token_length(fname->token),
                            oak_token_text(fname->token));
          return;
        }
        const int idx =
            find_struct_field(sd,
                              oak_token_text(fname->token),
                              (usize)oak_token_length(fname->token));
        if (idx < 0)
        {
          compiler_error_at(c,
                            fname->token,
                            "no such field '%.*s' on struct '%.*s'",
                            oak_token_length(fname->token),
                            oak_token_text(fname->token),
                            (int)sd->name_len,
                            sd->name);
          return;
        }

        struct oak_type_t val_ty;
        infer_expr_static_type(c, rhs, &val_ty);
        if (oak_type_is_known(&val_ty) &&
            !oak_type_equal(&sd->fields[idx].type, &val_ty))
        {
          compiler_error_at(c,
                            rhs->token ? rhs->token : fname->token,
                            "cannot assign value of type '%s' to field "
                            "'%.*s' of type '%s'",
                            type_full_name(c, val_ty),
                            (int)sd->fields[idx].name_len,
                            sd->fields[idx].name,
                            type_full_name(c, sd->fields[idx].type));
          return;
        }

        compile_node(c, recv);
        compile_node(c, rhs);
        emit_op_arg(
            c, OAK_OP_SET_FIELD, (u8)idx, code_loc_from_token(fname->token));
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
    case OAK_NODE_STMT_ADD_ASSIGN:
    case OAK_NODE_STMT_SUB_ASSIGN:
    case OAK_NODE_STMT_MUL_ASSIGN:
    case OAK_NODE_STMT_DIV_ASSIGN:
    case OAK_NODE_STMT_MOD_ASSIGN:
    {
      const struct oak_ast_node_t* lhs = node->lhs;
      const int slot = compile_assign_target(
          c, lhs, "compound assignment target must be a variable");
      if (slot < 0)
        return;

      if (node->kind == OAK_NODE_STMT_ADD_ASSIGN &&
          ast_is_int_literal(node->rhs, 1))
      {
        emit_op_arg(c,
                    OAK_OP_INC_LOCAL,
                    (u8)slot,
                    code_loc_from_token(lhs->token));
        break;
      }
      if (node->kind == OAK_NODE_STMT_SUB_ASSIGN &&
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
    case OAK_NODE_FN_CALL:
      compile_fn_call(c, node);
      break;
    case OAK_NODE_EXPR_EMPTY_ARRAY:
      compiler_error_at(
          c,
          null,
          "untyped array literal; arrays must be typed (e.g. '[] as "
          "number[]')");
      break;
    case OAK_NODE_EXPR_ARRAY_LITERAL:
    {
      const usize count = oak_list_length(&node->children);
      if (count == 0)
      {
        compiler_error_at(c,
                          null,
                          "internal error: array literal with no elements");
        return;
      }
      if (count > 255)
      {
        compiler_error_at(
            c, null, "array literal too large (max 255 elements)");
        return;
      }

      const struct oak_list_entry_t* first = node->children.next;
      const struct oak_ast_node_t* first_wrap =
          oak_container_of(first, struct oak_ast_node_t, link);
      const struct oak_ast_node_t* first_elem =
          first_wrap->kind == OAK_NODE_ARRAY_LITERAL_ELEMENT ? first_wrap->child
                                                             : first_wrap;

      struct oak_type_t elem_ty;
      infer_expr_static_type(c, first_elem, &elem_ty);
      if (!oak_type_is_known(&elem_ty))
      {
        compiler_error_at(
            c,
            first_elem ? first_elem->token : null,
            "cannot infer array element type from first element");
        return;
      }

      struct oak_list_entry_t* pos;
      oak_list_for_each(pos, &node->children)
      {
        const struct oak_ast_node_t* wrap =
            oak_container_of(pos, struct oak_ast_node_t, link);
        const struct oak_ast_node_t* elem =
            wrap->kind == OAK_NODE_ARRAY_LITERAL_ELEMENT ? wrap->child : wrap;

        struct oak_type_t et;
        infer_expr_static_type(c, elem, &et);
        if (oak_type_is_known(&et) && !oak_type_equal(&elem_ty, &et))
        {
          compiler_error_at(c,
                            elem ? elem->token : null,
                            "array literal element type mismatch "
                            "(expected '%s', got '%s')",
                            type_full_name(c, elem_ty),
                            type_full_name(c, et));
          return;
        }

        compile_node(c, elem);
        if (c->has_error)
          return;
      }

      emit_op_arg(c,
                  OAK_OP_NEW_ARRAY_FROM_STACK,
                  (u8)count,
                  OAK_LOC_SYNTHETIC);
      c->stack_depth -= (int)count;
      break;
    }
    case OAK_NODE_EXPR_EMPTY_MAP:
      compiler_error_at(
          c,
          null,
          "untyped map literal; maps must be typed (e.g. '[:] as "
          "[string:number]')");
      break;
    case OAK_NODE_EXPR_MAP_LITERAL:
    {
      const usize count = oak_list_length(&node->children);
      if (count == 0)
      {
        compiler_error_at(
            c, null, "internal error: map literal with no entries");
        return;
      }
      if (count > 255)
      {
        compiler_error_at(
            c, null, "map literal too large (max 255 entries)");
        return;
      }

      const struct oak_list_entry_t* first = node->children.next;
      const struct oak_ast_node_t* first_entry =
          oak_container_of(first, struct oak_ast_node_t, link);
      if (first_entry->kind != OAK_NODE_MAP_LITERAL_ENTRY ||
          !first_entry->lhs || !first_entry->rhs)
      {
        compiler_error_at(c, null, "malformed map literal entry");
        return;
      }

      struct oak_type_t key_ty;
      struct oak_type_t val_ty;
      infer_expr_static_type(c, first_entry->lhs, &key_ty);
      infer_expr_static_type(c, first_entry->rhs, &val_ty);
      if (!oak_type_is_known(&key_ty))
      {
        compiler_error_at(
            c,
            first_entry->lhs->token,
            "cannot infer map key type from first entry");
        return;
      }
      if (!oak_type_is_known(&val_ty))
      {
        compiler_error_at(
            c,
            first_entry->rhs->token,
            "cannot infer map value type from first entry");
        return;
      }

      struct oak_list_entry_t* pos;
      oak_list_for_each(pos, &node->children)
      {
        const struct oak_ast_node_t* entry =
            oak_container_of(pos, struct oak_ast_node_t, link);
        if (entry->kind != OAK_NODE_MAP_LITERAL_ENTRY || !entry->lhs ||
            !entry->rhs)
        {
          compiler_error_at(c, null, "malformed map literal entry");
          return;
        }

        struct oak_type_t kt;
        struct oak_type_t vt;
        infer_expr_static_type(c, entry->lhs, &kt);
        infer_expr_static_type(c, entry->rhs, &vt);
        if (oak_type_is_known(&kt) && !oak_type_equal(&key_ty, &kt))
        {
          compiler_error_at(c,
                            entry->lhs->token,
                            "map literal key type mismatch "
                            "(expected '%s', got '%s')",
                            type_full_name(c, key_ty),
                            type_full_name(c, kt));
          return;
        }
        if (oak_type_is_known(&vt) && !oak_type_equal(&val_ty, &vt))
        {
          compiler_error_at(c,
                            entry->rhs->token,
                            "map literal value type mismatch "
                            "(expected '%s', got '%s')",
                            type_full_name(c, val_ty),
                            type_full_name(c, vt));
          return;
        }

        compile_node(c, entry->lhs);
        if (c->has_error)
          return;
        compile_node(c, entry->rhs);
        if (c->has_error)
          return;
      }

      emit_op_arg(c,
                  OAK_OP_NEW_MAP_FROM_STACK,
                  (u8)count,
                  OAK_LOC_SYNTHETIC);
      c->stack_depth -= (int)count * 2;
      break;
    }
    case OAK_NODE_EXPR_CAST:
    {
      const struct oak_ast_node_t* value = node->lhs;
      const struct oak_ast_node_t* type_node = node->rhs;
      if (!value || !type_node)
      {
        compiler_error_at(c, null, "malformed 'as' expression");
        return;
      }

      if (type_node->kind == OAK_NODE_TYPE_ARRAY)
      {
        const struct oak_ast_node_t* elem = type_node->child;
        if (!elem || elem->kind != OAK_NODE_IDENT)
        {
          compiler_error_at(
              c, null, "array cast requires an element type (e.g. 'number[]')");
          return;
        }
        if (value->kind != OAK_NODE_EXPR_EMPTY_ARRAY)
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

      if (type_node->kind == OAK_NODE_TYPE_MAP)
      {
        const struct oak_ast_node_t* key = type_node->lhs;
        const struct oak_ast_node_t* val = type_node->rhs;
        if (!key || !val || key->kind != OAK_NODE_IDENT ||
            val->kind != OAK_NODE_IDENT)
        {
          compiler_error_at(c,
                            null,
                            "map cast requires key and value types "
                            "(e.g. '[string:number]')");
          return;
        }
        if (value->kind != OAK_NODE_EXPR_EMPTY_MAP)
        {
          compiler_error_at(c,
                            null,
                            "only empty map literals can be cast to a "
                            "map type (e.g. '[:] as [string:number]')");
          return;
        }
        emit_op(c, OAK_OP_NEW_MAP, OAK_LOC_SYNTHETIC);
        break;
      }

      compiler_error_at(c,
                        null,
                        "'as' is currently only supported for typing array "
                        "and map literals (e.g. '[] as number[]', "
                        "'[:] as [string:number]')");
      break;
    }
    case OAK_NODE_INDEX_ACCESS:
    {
      compile_node(c, node->lhs);
      compile_node(c, node->rhs);
      emit_op(c, OAK_OP_GET_INDEX, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_MEMBER_ACCESS:
    {
      const struct oak_ast_node_t* recv = node->lhs;
      const struct oak_ast_node_t* fname = node->rhs;
      if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
      {
        compiler_error_at(c,
                          node->token,
                          "field access requires the form 'expr.field'");
        return;
      }
      struct oak_type_t recv_ty;
      infer_expr_static_type(c, recv, &recv_ty);
      const struct oak_registered_struct_t* sd =
          oak_type_is_known(&recv_ty)
              ? find_struct_by_type_id(c, recv_ty.id)
              : null;
      if (!sd)
      {
        compiler_error_at(c,
                          fname->token,
                          "field access '.%.*s' requires a struct receiver",
                          oak_token_length(fname->token),
                          oak_token_text(fname->token));
        return;
      }
      const int idx =
          find_struct_field(sd,
                            oak_token_text(fname->token),
                            (usize)oak_token_length(fname->token));
      if (idx < 0)
      {
        compiler_error_at(c,
                          fname->token,
                          "no such field '%.*s' on struct '%.*s'",
                          oak_token_length(fname->token),
                          oak_token_text(fname->token),
                          (int)sd->name_len,
                          sd->name);
        return;
      }
      compile_node(c, recv);
      emit_op_arg(
          c, OAK_OP_GET_FIELD, (u8)idx, code_loc_from_token(fname->token));
      break;
    }
    case OAK_NODE_EXPR_STRUCT_LITERAL:
    {
      const struct oak_list_entry_t* first = node->children.next;
      if (first == &node->children)
      {
        compiler_error_at(
            c, node->token, "struct literal missing type name");
        return;
      }
      const struct oak_ast_node_t* name_node =
          oak_container_of(first, struct oak_ast_node_t, link);
      if (!name_node || name_node->kind != OAK_NODE_IDENT)
      {
        compiler_error_at(
            c, node->token, "struct literal: type must be an identifier");
        return;
      }
      const char* sname = oak_token_text(name_node->token);
      const int sname_len = oak_token_length(name_node->token);
      const struct oak_registered_struct_t* sd =
          find_struct_by_name(c, sname, (usize)sname_len);
      if (!sd)
      {
        compiler_error_at(c,
                          name_node->token,
                          "unknown struct type '%.*s'",
                          sname_len,
                          sname);
        return;
      }

      /* Collect the supplied initializers indexed by the field's declared
       * position so we can emit them in declaration order regardless of the
       * order they appear in the source. */
      const struct oak_ast_node_t* exprs[OAK_MAX_STRUCT_FIELDS] = { 0 };
      for (struct oak_list_entry_t* pos = first->next;
           pos != &node->children;
           pos = pos->next)
      {
        const struct oak_ast_node_t* entry =
            oak_container_of(pos, struct oak_ast_node_t, link);
        if (entry->kind != OAK_NODE_STRUCT_LITERAL_FIELD || !entry->lhs ||
            !entry->rhs)
        {
          compiler_error_at(
              c, entry->token, "malformed struct field initializer");
          return;
        }
        const struct oak_ast_node_t* fname = entry->lhs;
        const struct oak_ast_node_t* fexpr = entry->rhs;
        if (fname->kind != OAK_NODE_IDENT)
        {
          compiler_error_at(c,
                            fname->token,
                            "struct field name must be an identifier");
          return;
        }

        const int idx =
            find_struct_field(sd,
                              oak_token_text(fname->token),
                              (usize)oak_token_length(fname->token));
        if (idx < 0)
        {
          compiler_error_at(c,
                            fname->token,
                            "no such field '%.*s' on struct '%.*s'",
                            oak_token_length(fname->token),
                            oak_token_text(fname->token),
                            (int)sd->name_len,
                            sd->name);
          return;
        }
        if (exprs[idx])
        {
          compiler_error_at(c,
                            fname->token,
                            "duplicate field '%.*s' in struct literal",
                            oak_token_length(fname->token),
                            oak_token_text(fname->token));
          return;
        }

        struct oak_type_t got;
        infer_expr_static_type(c, fexpr, &got);
        if (oak_type_is_known(&got) &&
            !oak_type_equal(&sd->fields[idx].type, &got))
        {
          compiler_error_at(c,
                            fexpr->token ? fexpr->token : fname->token,
                            "field '%.*s': expected type '%s', got '%s'",
                            (int)sd->fields[idx].name_len,
                            sd->fields[idx].name,
                            type_full_name(c, sd->fields[idx].type),
                            type_full_name(c, got));
          return;
        }

        exprs[idx] = fexpr;
      }

      for (int i = 0; i < sd->field_count; ++i)
      {
        if (!exprs[i])
        {
          compiler_error_at(c,
                            name_node->token,
                            "missing field '%.*s' in '%.*s' literal",
                            (int)sd->fields[i].name_len,
                            sd->fields[i].name,
                            (int)sd->name_len,
                            sd->name);
          return;
        }
      }

      /* Emit the type-name string as a constant at the bottom so the runtime
       * can stamp it onto the new struct (cheap diagnostics). */
      struct oak_obj_string_t* type_name_obj =
          oak_string_new(sd->name, sd->name_len);
      const u8 name_idx = intern_constant(c, OAK_VALUE_OBJ(type_name_obj));
      emit_op_arg(c,
                  OAK_OP_CONSTANT,
                  name_idx,
                  code_loc_from_token(name_node->token));

      for (int i = 0; i < sd->field_count; ++i)
      {
        compile_node(c, exprs[i]);
        if (c->has_error)
          return;
      }

      emit_op_arg(c,
                  OAK_OP_NEW_STRUCT_FROM_STACK,
                  (u8)sd->field_count,
                  OAK_LOC_SYNTHETIC);
      c->stack_depth -= sd->field_count;
      break;
    }
    case OAK_NODE_STMT_IF:
      compile_stmt_if(c, node);
      break;
    case OAK_NODE_STMT_WHILE:
      compile_stmt_while(c, node);
      break;
    case OAK_NODE_STMT_FOR_FROM:
      compile_stmt_for_from(c, node);
      break;
    case OAK_NODE_STMT_FOR_IN:
      compile_stmt_for_in(c, node);
      break;
    case OAK_NODE_STMT_BREAK:
    case OAK_NODE_STMT_CONTINUE:
    {
      const int is_break = node->kind == OAK_NODE_STMT_BREAK;
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
    .map_method_count = 0,
  };
  oak_type_registry_init(&compiler.type_registry);

  if (!root || root->kind != OAK_NODE_PROGRAM)
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
