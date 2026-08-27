#include "internal/oak_compiler.h"
#include "internal/oak_compiler_modules.h"

int oak_compiler_type_is_refcounted(oak_compiler_t* c,
                                    const oak_type_t* ty)
{
  if (!oak_type_is_refcounted(ty))
    return 0;
  if (ty->kind == OAK_TYPE_KIND_SCALAR && ty->id >= OAK_TYPE_FIRST_USER)
  {
    const oak_registered_record_t* r =
        oak_records_find_by_id(&c->records, ty->id);
    if (r && r->is_value)
      return 0;
  }
  return 1;
}

/* Whether a reference of this type can be used to reach mutable state.
 *
 * The access model exists to stop a read-only reference being laundered into
 * a writable place and then written through. That is only a hazard for a type
 * something can be written through: a function is refcounted, because it is a
 * heap object, but it exposes no fields, no elements and no methods that
 * mutate it, so aliasing one grants nothing. */
int oak_compiler_type_carries_mutable_state(oak_compiler_t* c,
                                            const oak_type_t* ty)
{
  if (ty->kind == OAK_TYPE_KIND_FN)
    return 0;
  return oak_compiler_type_is_refcounted(c, ty);
}

static const oak_token_t* type_node_token(
    const oak_ast_node_t* type_node)
{
  if (!type_node)
    return OAK_NULL;
  if (type_node->token)
    return type_node->token;
  if (type_node->child)
    return type_node_token(type_node->child);
  if (type_node->lhs)
    return type_node_token(type_node->lhs);
  if (type_node->rhs)
    return type_node_token(type_node->rhs);
  const oak_list_entry_t* first = type_node->children.next;
  if (first != &type_node->children)
  {
    const oak_ast_node_t* child =
        OAK_CONTAINER_OF(first, oak_ast_node_t, link);
    return type_node_token(child);
  }
  return OAK_NULL;
}

void oak_lower_type_node(oak_compiler_t* c,
                                    const oak_ast_node_t* type_node,
                                    oak_type_t* out)
{
  oak_type_clear(out);
  if (!type_node)
    return;
  if (type_node->kind == OAK_NODE_TYPE_WEAK)
  {
    oak_lower_type_node(c, type_node->child, out);
    if (!oak_type_is_known(out))
      return;
    if (!oak_compiler_type_is_refcounted(c, out))
    {
      oak_compiler_error_at(
          c,
          type_node_token(type_node),
          "weak can only be applied to refcounted types");
      oak_type_clear(out);
      return;
    }
    if (out->id == OAK_TYPE_STRING)
    {
      oak_compiler_error_at(
          c,
          type_node_token(type_node),
          "weak cannot be applied to strings");
      oak_type_clear(out);
      return;
    }
    out->is_weak = 1;
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_WEAK_BASE)
  {
    const oak_ast_node_t* base =
        oak_ast_node_child_at(type_node, 0);
    if (base)
      oak_lower_type_node(c, base, out);
    return;
  }
  if (type_node->kind == OAK_NODE_IDENT)
  {
    const char* name = oak_token_text(type_node->token);
    const oak_registered_interface_t* tr = oak_interface_find(&c->interfaces, name);
    if (tr)
    {
      out->id = tr->interface_id;
      out->kind = OAK_TYPE_KIND_INTERFACE;
      return;
    }
    out->id = oak_intern_type_tok(c, type_node->token);
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_FN)
  {
    out->id = OAK_TYPE_FN;
    out->kind = OAK_TYPE_KIND_FN;
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_ARRAY)
  {
    const oak_ast_node_t* elem = type_node->child;
    if (!elem)
      return;
    if (elem->kind != OAK_NODE_IDENT)
      return;
    out->id = oak_intern_type_tok(c, elem->token);
    out->kind = OAK_TYPE_KIND_ARRAY;
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_MAP)
  {
    const oak_ast_node_t* key = type_node->lhs;
    const oak_ast_node_t* val = type_node->rhs;
    if (!key || !val || key->kind != OAK_NODE_IDENT ||
        val->kind != OAK_NODE_IDENT)
      return;
    out->key_id = oak_intern_type_tok(c, key->token);
    out->id = oak_intern_type_tok(c, val->token);
    out->kind = OAK_TYPE_KIND_MAP;
    return;
  }
}

int oak_type_accepts(const oak_type_t* want,
                      const oak_type_t* got)
{
  if (oak_type_equal(want, got))
    return 1;
  if (want->is_weak && !got->is_weak && oak_type_equal_base(want, got))
    return 1;
  if (want->is_weak && got->id == OAK_TYPE_NONE)
    return 1;
  return 0;
}

oak_type_id_t oak_intern_type_tok(oak_compiler_t* c,
                                             const oak_token_t* token)
{
  return oak_type_registry_intern(&c->types, oak_token_text(token));
}

int oak_local_type_get(oak_compiler_t* c,
                                const char* name,
                                oak_type_t* out)
{
  for (int i = c->scope.local_count - 1; i >= 0; --i)
  {
    const oak_local_t* L = &c->scope.locals[i];
    if (oak_name_eq(L->name, name))
    {
      *out = L->type;
      return 1;
    }
  }
  return 0;
}
