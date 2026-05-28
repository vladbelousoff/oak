#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

void oak_compiler_compile_array_literal(struct oak_compiler_t* c,
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
  oak_infer_type(c, first_elem, &elem_ty);
  if (!oak_type_is_known(&elem_ty))
  {
    oak_compiler_error_at(c,
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
    oak_infer_type(c, elem, &et);
    if (oak_type_is_known(&et) && !oak_type_equal(&elem_ty, &et))
    {
      oak_compiler_error_at(c,
                            elem ? elem->token : null,
                            "array literal element type mismatch "
                            "(expected '%s', got '%s')",
                            oak_type_full_name(c, elem_ty),
                            oak_type_full_name(c, et));
      return;
    }

    oak_compiler_compile_node(c, elem);
    if (c->has_error)
      return;
  }

  oak_compiler_emit_op(
      c, OAK_OP_NEW_ARR, OAK_LOC_SYNTHETIC, OAK_ARG_U8((u8)count));
  c->scope.stack_depth -= (int)count;
}

void oak_compiler_compile_map_literal(struct oak_compiler_t* c,
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
  oak_infer_type(c, first_entry->lhs, &key_ty);
  oak_infer_type(c, first_entry->rhs, &val_ty);
  if (!oak_type_is_known(&key_ty))
  {
    oak_compiler_error_at(c,
                          first_entry->lhs->token,
                          "cannot infer map key type from first entry");
    return;
  }
  if (key_ty.id == OAK_TYPE_NONE || key_ty.is_weak)
  {
    oak_compiler_error_at(c,
                          first_entry->lhs->token,
                          "'none' and weak references cannot be used as map keys");
    return;
  }
  if (!oak_type_is_known(&val_ty))
  {
    oak_compiler_error_at(c,
                          first_entry->rhs->token,
                          "cannot infer map value type from first entry");
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
    oak_infer_type(c, entry->lhs, &kt);
    oak_infer_type(c, entry->rhs, &vt);
    if (oak_type_is_known(&kt) && !oak_type_equal(&key_ty, &kt))
    {
      oak_compiler_error_at(c,
                            entry->lhs->token,
                            "map literal key type mismatch "
                            "(expected '%s', got '%s')",
                            oak_type_full_name(c, key_ty),
                            oak_type_full_name(c, kt));
      return;
    }
    if (oak_type_is_known(&vt) && !oak_type_equal(&val_ty, &vt))
    {
      oak_compiler_error_at(c,
                            entry->rhs->token,
                            "map literal value type mismatch "
                            "(expected '%s', got '%s')",
                            oak_type_full_name(c, val_ty),
                            oak_type_full_name(c, vt));
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

  oak_compiler_emit_op(
      c, OAK_OP_NEW_MAP, OAK_LOC_SYNTHETIC, OAK_ARG_U8((u8)count));
  c->scope.stack_depth -= (int)count * 2;
}

void oak_compiler_compile_cast(struct oak_compiler_t* c,
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
      oak_compiler_error_at(c,
                            null,
                            "only empty array literals can be cast to an "
                            "array type (e.g. '[] as number[]')");
      return;
    }
    oak_compiler_emit_op(c, OAK_OP_NEW_ARR, OAK_LOC_SYNTHETIC, OAK_ARG_U8(0));
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
          c,
          null,
          "map cast requires key and value types (e.g. '[string:number]')");
      return;
    }
    if (value->kind != OAK_NODE_EXPR_EMPTY_MAP)
    {
      oak_compiler_error_at(c,
                            null,
                            "only empty map literals can be cast to a "
                            "map type (e.g. '[:] as [string:number]')");
      return;
    }
    oak_compiler_emit_op(c, OAK_OP_NEW_MAP, OAK_LOC_SYNTHETIC, OAK_ARG_U8(0));
    return;
  }

  oak_compiler_error_at(c,
                        null,
                        "'as' is currently only supported for typing array "
                        "and map literals (e.g. '[] as number[]', "
                        "'[:] as [string:number]')");
}
