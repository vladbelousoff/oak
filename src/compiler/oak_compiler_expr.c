#include "oak_compiler_internal.h"

usize oak_compiler_ast_child_count(const struct oak_ast_node_t* node)
{
  if (oak_node_is_unary_op(node->kind))
    return node->child ? 1u : 0u;
  if (oak_node_is_binary_op(node->kind))
    return (usize)(node->lhs ? 1 : 0) + (usize)(node->rhs ? 1 : 0);
  return oak_list_length(&node->children);
}

int oak_compiler_ast_is_int_literal(const struct oak_ast_node_t* node,
                              const int value)
{
  return node && node->kind == OAK_NODE_INT &&
         oak_token_as_i32(node->token) == value;
}

static void reject_binary_void(struct oak_compiler_t* c,
                                const struct oak_ast_node_t* node)
{
  oak_compiler_reject_void_value_expr(c, node->lhs);
  if (c->has_error)
    return;
  oak_compiler_reject_void_value_expr(c, node->rhs);
}

u8 oak_compiler_opcode_for_node_kind(const enum oak_node_kind_t kind)
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

static void compile_stmt_assignment(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* lhs = node->lhs;
  const struct oak_ast_node_t* rhs = node->rhs;

  if (lhs->kind == OAK_NODE_INDEX_ACCESS)
  {
    struct oak_type_t coll_ty;
    oak_compiler_infer_expr_static_type(c, lhs->lhs, &coll_ty);
    if (coll_ty.kind == OAK_TYPE_KIND_SCALAR || !oak_type_is_known(&coll_ty))
    {
      oak_compiler_error_at(
          c, lhs->lhs->token, "indexed assignment requires a typed array or map");
      return;
    }

    if (coll_ty.kind == OAK_TYPE_KIND_MAP)
    {
      struct oak_type_t key_ty;
      oak_compiler_infer_expr_static_type(c, lhs->rhs, &key_ty);
      if (oak_type_is_known(&key_ty))
      {
        const struct oak_type_t want_key = { .id = coll_ty.key_id };
        if (!oak_type_equal(&want_key, &key_ty))
        {
          oak_compiler_error_at(c, lhs->rhs->token,
                                "map key must be of type '%s', got '%s'",
                                oak_compiler_type_full_name(c, want_key),
                                oak_compiler_type_full_name(c, key_ty));
          return;
        }
      }
    }

    struct oak_type_t val_ty;
    oak_compiler_infer_expr_static_type(c, rhs, &val_ty);
    if (oak_type_is_void(&val_ty))
    {
      oak_compiler_error_at(
          c, rhs->token, "cannot assign void to an indexed value");
      return;
    }
    if (oak_type_is_known(&val_ty))
    {
      const struct oak_type_t element_ty = { .id = coll_ty.id };
      if (!oak_type_equal(&element_ty, &val_ty))
      {
        oak_compiler_error_at(
            c, rhs->token,
            "cannot assign value of type '%s' to element of '%s' %s",
            oak_compiler_type_full_name(c, val_ty),
            oak_compiler_type_full_name(c, element_ty),
            coll_ty.kind == OAK_TYPE_KIND_MAP ? "map" : "array");
        return;
      }
    }

    oak_compiler_compile_node(c, lhs->lhs);
    oak_compiler_compile_node(c, lhs->rhs);
    oak_compiler_compile_node(c, rhs);
    oak_compiler_emit_op(c, OAK_OP_SET_INDEX, OAK_LOC_SYNTHETIC);
    oak_compiler_emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
    return;
  }

  if (lhs->kind == OAK_NODE_MEMBER_ACCESS)
  {
    const struct oak_ast_node_t* recv = lhs->lhs;
    const struct oak_ast_node_t* fname = lhs->rhs;
    if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, lhs->token, "field assignment requires 'expr.field = expr'");
      return;
    }
    const struct oak_registered_struct_t* sd = null;
    const int idx =
        oak_compiler_require_struct_field(c, recv, fname, 1, &sd);
    if (idx < 0)
      return;

    struct oak_type_t val_ty;
    oak_compiler_infer_expr_static_type(c, rhs, &val_ty);
    if (oak_type_is_void(&val_ty))
    {
      oak_compiler_error_at(
          c, rhs->token ? rhs->token : fname->token,
          "cannot assign void to a struct field");
      return;
    }
    if (oak_type_is_known(&val_ty) &&
        !oak_type_equal(&sd->fields[idx].type, &val_ty))
    {
      oak_compiler_error_at(
          c, rhs->token ? rhs->token : fname->token,
          "cannot assign value of type '%s' to field '%s' of type '%s'",
          oak_compiler_type_full_name(c, val_ty), sd->fields[idx].name,
          oak_compiler_type_full_name(c, sd->fields[idx].type));
      return;
    }

    oak_compiler_compile_node(c, recv);
    oak_compiler_compile_node(c, rhs);
    oak_compiler_emit_op_arg(
        c, OAK_OP_SET_FIELD, (u8)idx, oak_compiler_loc_from_token(fname->token));
    oak_compiler_emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
    return;
  }

  const int slot = oak_compiler_compile_assign_target(
      c, lhs, "assignment target must be a variable");
  if (slot < 0)
    return;

  oak_compiler_reject_void_value_expr(c, rhs);
  if (c->has_error)
    return;

  oak_compiler_compile_node(c, rhs);
  oak_compiler_emit_op_arg(
      c, OAK_OP_SET_LOCAL, (u8)slot, oak_compiler_loc_from_token(lhs->token));
  oak_compiler_emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
}

static void compile_expr_array_literal(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node)
{
  const usize count = oak_list_length(&node->children);
  if (count == 0)
  {
    oak_compiler_error_at(
        c, null, "internal error: array literal with no elements");
    return;
  }
  if (count > 255)
  {
    oak_compiler_error_at(
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
  oak_compiler_infer_expr_static_type(c, first_elem, &elem_ty);
  if (!oak_type_is_known(&elem_ty))
  {
    oak_compiler_error_at(
        c, first_elem ? first_elem->token : null,
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
    oak_compiler_infer_expr_static_type(c, elem, &et);
    if (oak_type_is_known(&et) && !oak_type_equal(&elem_ty, &et))
    {
      oak_compiler_error_at(
          c, elem ? elem->token : null,
          "array literal element type mismatch "
          "(expected '%s', got '%s')",
          oak_compiler_type_full_name(c, elem_ty),
          oak_compiler_type_full_name(c, et));
      return;
    }

    oak_compiler_compile_node(c, elem);
    if (c->has_error)
      return;
  }

  oak_compiler_emit_op_arg(
      c, OAK_OP_NEW_ARRAY_FROM_STACK, (u8)count, OAK_LOC_SYNTHETIC);
  c->scope.stack_depth -= (int)count;
}

static void compile_expr_map_literal(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* first_entry = node->lhs;
  const struct oak_ast_node_t* more = node->rhs;
  if (!first_entry || !more || more->kind != OAK_NODE_MAP_LITERAL_ENTRIES)
  {
    oak_compiler_error_at(c, null, "malformed map literal");
    return;
  }

  const usize count = 1u + oak_list_length(&more->children);
  if (count > 255)
  {
    oak_compiler_error_at(c, null, "map literal too large (max 255 entries)");
    return;
  }

  if (first_entry->kind != OAK_NODE_MAP_LITERAL_ENTRY || !first_entry->lhs ||
      !first_entry->rhs)
  {
    oak_compiler_error_at(c, null, "malformed map literal entry");
    return;
  }

  struct oak_type_t key_ty;
  struct oak_type_t val_ty;
  oak_compiler_infer_expr_static_type(c, first_entry->lhs, &key_ty);
  oak_compiler_infer_expr_static_type(c, first_entry->rhs, &val_ty);
  if (!oak_type_is_known(&key_ty))
  {
    oak_compiler_error_at(
        c, first_entry->lhs->token, "cannot infer map key type from first entry");
    return;
  }
  if (!oak_type_is_known(&val_ty))
  {
    oak_compiler_error_at(
        c, first_entry->rhs->token, "cannot infer map value type from first entry");
    return;
  }

  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &more->children)
  {
    const struct oak_ast_node_t* entry =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (entry->kind != OAK_NODE_MAP_LITERAL_ENTRY || !entry->lhs || !entry->rhs)
    {
      oak_compiler_error_at(c, null, "malformed map literal entry");
      return;
    }

    struct oak_type_t kt;
    struct oak_type_t vt;
    oak_compiler_infer_expr_static_type(c, entry->lhs, &kt);
    oak_compiler_infer_expr_static_type(c, entry->rhs, &vt);
    if (oak_type_is_known(&kt) && !oak_type_equal(&key_ty, &kt))
    {
      oak_compiler_error_at(
          c, entry->lhs->token,
          "map literal key type mismatch "
          "(expected '%s', got '%s')",
          oak_compiler_type_full_name(c, key_ty),
          oak_compiler_type_full_name(c, kt));
      return;
    }
    if (oak_type_is_known(&vt) && !oak_type_equal(&val_ty, &vt))
    {
      oak_compiler_error_at(
          c, entry->rhs->token,
          "map literal value type mismatch "
          "(expected '%s', got '%s')",
          oak_compiler_type_full_name(c, val_ty),
          oak_compiler_type_full_name(c, vt));
      return;
    }
  }

  oak_compiler_compile_node(c, first_entry->lhs);
  if (c->has_error)
    return;
  oak_compiler_compile_node(c, first_entry->rhs);
  if (c->has_error)
    return;
  oak_list_for_each(pos, &more->children)
  {
    const struct oak_ast_node_t* entry =
        oak_container_of(pos, struct oak_ast_node_t, link);
    oak_compiler_compile_node(c, entry->lhs);
    if (c->has_error)
      return;
    oak_compiler_compile_node(c, entry->rhs);
    if (c->has_error)
      return;
  }

  oak_compiler_emit_op_arg(
      c, OAK_OP_NEW_MAP_FROM_STACK, (u8)count, OAK_LOC_SYNTHETIC);
  c->scope.stack_depth -= (int)count * 2;
}

static void compile_expr_cast(struct oak_compiler_t* c,
                              const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* value = node->lhs;
  const struct oak_ast_node_t* type_node = node->rhs;
  if (!value || !type_node)
  {
    oak_compiler_error_at(c, null, "malformed 'as' expression");
    return;
  }

  if (type_node->kind == OAK_NODE_TYPE_ARRAY)
  {
    const struct oak_ast_node_t* elem = type_node->child;
    if (!elem || elem->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, null, "array cast requires an element type (e.g. 'number[]')");
      return;
    }
    if (value->kind != OAK_NODE_EXPR_EMPTY_ARRAY)
    {
      oak_compiler_error_at(
          c, null, "only empty array literals can be cast to an "
                  "array type (e.g. '[] as number[]')");
      return;
    }
    oak_compiler_emit_op(c, OAK_OP_NEW_ARRAY, OAK_LOC_SYNTHETIC);
    return;
  }

  if (type_node->kind == OAK_NODE_TYPE_MAP)
  {
    const struct oak_ast_node_t* key = type_node->lhs;
    const struct oak_ast_node_t* val = type_node->rhs;
    if (!key || !val || key->kind != OAK_NODE_IDENT ||
        val->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, null, "map cast requires key and value types (e.g. '[string:number]')");
      return;
    }
    if (value->kind != OAK_NODE_EXPR_EMPTY_MAP)
    {
      oak_compiler_error_at(
          c, null, "only empty map literals can be cast to a "
                  "map type (e.g. '[:] as [string:number]')");
      return;
    }
    oak_compiler_emit_op(c, OAK_OP_NEW_MAP, OAK_LOC_SYNTHETIC);
    return;
  }

  oak_compiler_error_at(
      c, null, "'as' is currently only supported for typing array "
              "and map literals (e.g. '[] as number[]', "
              "'[:] as [string:number]')");
}

static void compile_expr_member_access(struct oak_compiler_t* c,
                                      const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* recv = node->lhs;
  const struct oak_ast_node_t* fname = node->rhs;
  if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, node->token, "field access requires the form 'expr.field'");
    return;
  }

  /* Enum variant access: EnumName.Variant */
  if (recv->kind == OAK_NODE_IDENT)
  {
    const char* recv_name = oak_token_text(recv->token);
    const usize recv_len = oak_token_length(recv->token);
    if (oak_compiler_is_enum_name(c, recv_name, recv_len))
    {
      const char* vname = oak_token_text(fname->token);
      const usize vlen = oak_token_length(fname->token);
      const struct oak_enum_variant_t* ev =
          oak_compiler_find_enum_variant_qualified(
              c, recv_name, recv_len, vname, vlen);
      if (!ev)
      {
        oak_compiler_error_at(c, fname->token,
                              "'%s' is not a variant of enum '%s'",
                              vname, recv_name);
        return;
      }
      oak_compiler_emit_constant(
          c, ev->const_idx, oak_compiler_loc_from_token(fname->token));
      return;
    }
  }

  oak_compiler_reject_void_value_expr(c, recv);
  if (c->has_error)
    return;
  const struct oak_registered_struct_t* sd = null;
  const int idx = oak_compiler_require_struct_field(c, recv, fname, 0, &sd);
  (void)sd;
  if (idx < 0)
    return;
  oak_compiler_compile_node(c, recv);
  oak_compiler_emit_op_arg(
      c, OAK_OP_GET_FIELD, (u8)idx, oak_compiler_loc_from_token(fname->token));
}

static void compile_expr_struct_literal(struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* name_node = node->lhs;
  const struct oak_ast_node_t* fields_node = node->rhs;
  if (!name_node || name_node->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, node->token, "struct literal: type must be an identifier");
    return;
  }
  if (!fields_node || fields_node->kind != OAK_NODE_STRUCT_LITERAL_FIELDS)
  {
    oak_compiler_error_at(
        c, node->token, "struct literal: malformed field list");
    return;
  }
  const char* sname = oak_token_text(name_node->token);
  const usize sname_len = oak_token_length(name_node->token);
  const struct oak_registered_struct_t* sd =
      oak_compiler_find_struct_by_name(c, sname, sname_len);
  if (!sd)
  {
    oak_compiler_error_at(
        c, name_node->token, "unknown struct type '%s'", sname);
    return;
  }

  const struct oak_ast_node_t* exprs[OAK_MAX_STRUCT_FIELDS] = { 0 };
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &fields_node->children)
  {
    const struct oak_ast_node_t* entry =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (entry->kind != OAK_NODE_STRUCT_LITERAL_FIELD || !entry->lhs ||
        !entry->rhs)
    {
      oak_compiler_error_at(
          c, entry->token, "malformed struct field initializer");
      return;
    }
    const struct oak_ast_node_t* fname = entry->lhs;
    const struct oak_ast_node_t* fexpr = entry->rhs;
    if (fname->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, fname->token, "struct field name must be an identifier");
      return;
    }

    const usize fname_len = oak_token_length(fname->token);
    const int idx = oak_compiler_find_struct_field(
        sd, oak_token_text(fname->token), fname_len);
    if (idx < 0)
    {
      oak_compiler_error_at(
          c, fname->token, "no such field '%s' on struct '%s'",
          oak_token_text(fname->token), sd->name);
      return;
    }
    if (exprs[idx])
    {
      oak_compiler_error_at(
          c, fname->token, "duplicate field '%s' in struct literal",
          oak_token_text(fname->token));
      return;
    }

    struct oak_type_t got;
    oak_compiler_infer_expr_static_type(c, fexpr, &got);
    if (oak_type_is_known(&got) &&
        !oak_type_equal(&sd->fields[idx].type, &got))
    {
      oak_compiler_error_at(
          c, fexpr->token ? fexpr->token : fname->token,
          "field '%s': expected type '%s', got '%s'", sd->fields[idx].name,
          oak_compiler_type_full_name(c, sd->fields[idx].type),
          oak_compiler_type_full_name(c, got));
      return;
    }

    exprs[idx] = fexpr;
  }

  for (int i = 0; i < sd->field_count; ++i)
  {
    if (!exprs[i])
    {
      oak_compiler_error_at(
          c, name_node->token, "missing field '%s' in '%s' literal",
          sd->fields[i].name, sd->name);
      return;
    }
  }

  struct oak_obj_string_t* type_name_obj =
      oak_string_new(sd->name, sd->name_len);
  const u16 name_idx =
      oak_compiler_intern_constant(c, OAK_VALUE_OBJ(type_name_obj));
  oak_compiler_emit_constant(
      c, name_idx, oak_compiler_loc_from_token(name_node->token));

  for (int i = 0; i < sd->field_count; ++i)
  {
    oak_compiler_compile_node(c, exprs[i]);
    if (c->has_error)
      return;
  }

  oak_compiler_emit_op_arg(
      c, OAK_OP_NEW_STRUCT_FROM_STACK, (u8)sd->field_count, OAK_LOC_SYNTHETIC);
  c->scope.stack_depth -= sd->field_count;
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
      oak_compiler_compile_stmt_return(c, node);
      break;
    case OAK_NODE_INT:
    {
      const int value = oak_token_as_i32(node->token);
      const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_I32(value));
      oak_compiler_emit_constant(c, idx, oak_compiler_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_FLOAT:
    {
      const float value = oak_token_as_f32(node->token);
      const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_F32(value));
      oak_compiler_emit_constant(c, idx, oak_compiler_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_STRING:
    {
      const char* chars = oak_token_text(node->token);
      const usize len = oak_token_length(node->token);
      struct oak_obj_string_t* str = oak_string_new(chars, len);
      const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_OBJ(str));
      oak_compiler_emit_constant(c, idx, oak_compiler_loc_from_token(node->token));
      break;
    }
    case OAK_NODE_IDENT:
    {
      const char* name = oak_token_text(node->token);
      const usize len = oak_token_length(node->token);
      const int slot = oak_compiler_find_local(c, name, len, null);
      if (slot >= 0)
      {
        oak_compiler_emit_op_arg(
            c, OAK_OP_GET_LOCAL, (u8)slot, oak_compiler_loc_from_token(node->token));
        break;
      }
      oak_compiler_error_at(c, node->token, "undefined variable '%s'", name);
      return;
    }
    case OAK_NODE_SELF:
    {
      const int slot = oak_compiler_find_local(c, "self", 4u, null);
      if (slot < 0)
      {
        oak_compiler_error_at(
            c, node->token, "'self' is only valid inside a method body");
        return;
      }
      oak_compiler_emit_op_arg(
          c, OAK_OP_GET_LOCAL, (u8)slot, oak_compiler_loc_from_token(node->token));
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
    {
      reject_binary_void(c, node);
      if (c->has_error)
        return;
      oak_compiler_compile_node(c, node->lhs);
      oak_compiler_compile_node(c, node->rhs);
      oak_compiler_emit_op(c,
              oak_compiler_opcode_for_node_kind(node->kind),
              oak_compiler_loc_from_token(node->lhs->token));
      break;
    }
    case OAK_NODE_BINARY_AND:
    {
      reject_binary_void(c, node);
      if (c->has_error)
        return;
      /* Short-circuit &&:
       *   evaluate lhs
       *   JUMP_IF_FALSE [false_branch]   ; pops lhs; jump if lhs is falsy
       *   evaluate rhs
       *   NOT, NOT                       ; normalise rhs to bool
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
      oak_compiler_emit_op(c, OAK_OP_NOT, loc);
      oak_compiler_emit_op(c, OAK_OP_NOT, loc);
      const usize end_jump = oak_compiler_emit_jump(c, OAK_OP_JUMP, loc);
      oak_compiler_patch_jump(c, false_jump);
      c->scope.stack_depth = depth_after_jif;
      oak_compiler_emit_op(c, OAK_OP_FALSE, loc);
      oak_compiler_patch_jump(c, end_jump);
      break;
    }
    case OAK_NODE_BINARY_OR:
    {
      reject_binary_void(c, node);
      if (c->has_error)
        return;
      /* Short-circuit ||:
       *   evaluate lhs
       *   NOT                            ; invert lhs for JUMP_IF_FALSE
       *   JUMP_IF_FALSE [true_branch]    ; pops !lhs; jump if lhs was truthy
       *   evaluate rhs
       *   NOT, NOT                       ; normalise rhs to bool
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
      oak_compiler_emit_op(c, OAK_OP_NOT, loc);
      const usize true_jump =
          oak_compiler_emit_jump(c, OAK_OP_JUMP_IF_FALSE, loc);
      const int depth_after_jif = c->scope.stack_depth;
      oak_compiler_compile_node(c, node->rhs);
      if (c->has_error)
        return;
      oak_compiler_emit_op(c, OAK_OP_NOT, loc);
      oak_compiler_emit_op(c, OAK_OP_NOT, loc);
      const usize end_jump = oak_compiler_emit_jump(c, OAK_OP_JUMP, loc);
      oak_compiler_patch_jump(c, true_jump);
      c->scope.stack_depth = depth_after_jif;
      oak_compiler_emit_op(c, OAK_OP_TRUE, loc);
      oak_compiler_patch_jump(c, end_jump);
      break;
    }
    case OAK_NODE_UNARY_NEG:
    case OAK_NODE_UNARY_NOT:
    {
      oak_compiler_reject_void_value_expr(c, node->child);
      if (c->has_error)
        return;
      oak_compiler_compile_node(c, node->child);
      oak_compiler_emit_op(c,
              oak_compiler_opcode_for_node_kind(node->kind),
              oak_compiler_loc_from_token(node->child->token));
      break;
    }
    case OAK_NODE_STMT_EXPR:
    {
      const int depth_before = c->scope.stack_depth;
      struct oak_ast_node_t* expr = node->child;
      oak_compiler_compile_node(c, expr);
      if (c->scope.stack_depth > depth_before)
        oak_compiler_emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_STMT_LET_ASSIGNMENT:
    {
      /* STMT_LET_ASSIGNMENT is BINARY: lhs = MUT_KEYWORD? (non-null iff
       * mutable), rhs = STMT_ASSIGNMENT. */
      const int is_mutable = node->lhs != null;
      const struct oak_ast_node_t* assign = node->rhs;

      if (!assign || assign->kind != OAK_NODE_STMT_ASSIGNMENT)
      {
        oak_compiler_error_at(c, null, "malformed 'let' statement");
        return;
      }

      const struct oak_ast_node_t* ident = assign->lhs;
      const struct oak_ast_node_t* rhs = assign->rhs;

      struct oak_type_t rhs_ty;
      oak_compiler_infer_expr_static_type(c, rhs, &rhs_ty);

      if (is_mutable && oak_type_is_refcounted(&rhs_ty) &&
          !oak_compiler_expr_is_mutable_place(c, rhs))
      {
        oak_compiler_error_at(c,
                              rhs->token,
                              "cannot create a mutable binding from an "
                              "immutable expression");
        return;
      }

      oak_compiler_reject_void_value_expr(c, rhs);
      if (c->has_error)
        return;

      oak_compiler_compile_node(c, rhs);
      const char* name = oak_token_text(ident->token);
      const usize name_len = oak_token_length(ident->token);
      oak_compiler_add_local(
          c, name, name_len, c->scope.stack_depth - 1, is_mutable, rhs_ty);

      break;
    }
    case OAK_NODE_STMT_ASSIGNMENT:
      compile_stmt_assignment(c, node);
      break;
    case OAK_NODE_STMT_ADD_ASSIGN:
    case OAK_NODE_STMT_SUB_ASSIGN:
    case OAK_NODE_STMT_MUL_ASSIGN:
    case OAK_NODE_STMT_DIV_ASSIGN:
    case OAK_NODE_STMT_MOD_ASSIGN:
    {
      const struct oak_ast_node_t* lhs = node->lhs;
      const int slot = oak_compiler_compile_assign_target(
          c, lhs, "compound assignment target must be a variable");
      if (slot < 0)
        return;

      if (node->kind == OAK_NODE_STMT_ADD_ASSIGN &&
          oak_compiler_ast_is_int_literal(node->rhs, 1))
      {
        oak_compiler_emit_op_arg(c,
                    OAK_OP_INC_LOCAL,
                    (u8)slot,
                    oak_compiler_loc_from_token(lhs->token));
        break;
      }
      if (node->kind == OAK_NODE_STMT_SUB_ASSIGN &&
          oak_compiler_ast_is_int_literal(node->rhs, 1))
      {
        oak_compiler_emit_op_arg(c,
                    OAK_OP_DEC_LOCAL,
                    (u8)slot,
                    oak_compiler_loc_from_token(lhs->token));
        break;
      }

      oak_compiler_reject_void_value_expr(c, node->rhs);
      if (c->has_error)
        return;

      oak_compiler_emit_op_arg(
          c, OAK_OP_GET_LOCAL, (u8)slot, oak_compiler_loc_from_token(lhs->token));
      oak_compiler_compile_node(c, node->rhs);
      oak_compiler_emit_op(
          c, oak_compiler_opcode_for_node_kind(node->kind), oak_compiler_loc_from_token(lhs->token));
      oak_compiler_emit_op_arg(
          c, OAK_OP_SET_LOCAL, (u8)slot, oak_compiler_loc_from_token(lhs->token));
      oak_compiler_emit_op(c, OAK_OP_POP, OAK_LOC_SYNTHETIC);
      break;
    }
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
      compile_expr_array_literal(c, node);
      break;
    case OAK_NODE_EXPR_EMPTY_MAP:
      oak_compiler_error_at(
          c,
          null,
          "untyped map literal; maps must be typed (e.g. '[:] as "
          "[string:number]')");
      break;
    case OAK_NODE_EXPR_MAP_LITERAL:
      compile_expr_map_literal(c, node);
      break;
    case OAK_NODE_EXPR_CAST:
      compile_expr_cast(c, node);
      break;
    case OAK_NODE_INDEX_ACCESS:
    {
      oak_compiler_reject_void_value_expr(c, node->lhs);
      if (c->has_error)
        return;
      oak_compiler_reject_void_value_expr(c, node->rhs);
      if (c->has_error)
        return;
      oak_compiler_compile_node(c, node->lhs);
      oak_compiler_compile_node(c, node->rhs);
      oak_compiler_emit_op(c, OAK_OP_GET_INDEX, OAK_LOC_SYNTHETIC);
      break;
    }
    case OAK_NODE_MEMBER_ACCESS:
      compile_expr_member_access(c, node);
      break;
    case OAK_NODE_EXPR_STRUCT_LITERAL:
      compile_expr_struct_literal(c, node);
      break;
    case OAK_NODE_STMT_IF:
      oak_compiler_compile_stmt_if(c, node);
      break;
    case OAK_NODE_STMT_WHILE:
      oak_compiler_compile_stmt_while(c, node);
      break;
    case OAK_NODE_STMT_FOR_FROM:
      oak_compiler_compile_stmt_for_from(c, node);
      break;
    case OAK_NODE_STMT_FOR_IN:
      oak_compiler_compile_stmt_for_in(c, node);
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
      oak_compiler_emit_loop_control_jump(
          c,
          is_break ? loop->break_jumps : loop->continue_jumps,
          is_break ? &loop->break_count : &loop->continue_count,
          is_break ? loop->exit_depth : loop->continue_depth,
          keyword);
      break;
    }
    default:
      oak_compiler_error_at(c, null, "unsupported AST node kind (%d)", node->kind);
      break;
  }
}
