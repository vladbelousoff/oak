#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

static const struct oak_token_t* type_node_token(
    const struct oak_ast_node_t* type_node)
{
  if (!type_node)
    return null;
  if (type_node->token)
    return type_node->token;
  if (type_node->child)
    return type_node_token(type_node->child);
  if (type_node->lhs)
    return type_node_token(type_node->lhs);
  if (type_node->rhs)
    return type_node_token(type_node->rhs);
  const struct oak_list_entry_t* first = type_node->children.next;
  if (first != &type_node->children)
  {
    const struct oak_ast_node_t* child =
        oak_container_of(first, struct oak_ast_node_t, link);
    return type_node_token(child);
  }
  return null;
}

void oakc_lower_type_node(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* type_node,
                                    struct oak_type_t* out)
{
  oak_type_clear(out);
  if (!type_node)
    return;
  if (type_node->kind == OAK_NODE_TYPE_WEAK)
  {
    oakc_lower_type_node(c, type_node->child, out);
    if (!oak_type_is_known(out))
      return;
    if (!oak_type_is_refcounted(out))
    {
      oak_compiler_error_at(
          c,
          type_node_token(type_node),
          "weak can only be applied to refcounted types");
      oak_type_clear(out);
      return;
    }
    out->is_weak = 1;
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_WEAK_BASE)
  {
    const struct oak_ast_node_t* base =
        oak_ast_node_child_at(type_node, 0);
    if (base)
      oakc_lower_type_node(c, base, out);
    return;
  }
  if (type_node->kind == OAK_NODE_IDENT)
  {
    const char* name = oak_token_text(type_node->token);
    const usize len = oak_token_length(type_node->token);
    const struct oak_registered_trait_t* tr = oakc_trait_find(&c->traits, name, len);
    if (tr)
    {
      out->id = tr->trait_id;
      out->kind = OAK_TYPE_KIND_TRAIT;
      return;
    }
    out->id = oakc_intern_type_tok(c, type_node->token);
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_ARRAY)
  {
    const struct oak_ast_node_t* elem = type_node->child;
    if (!elem || elem->kind != OAK_NODE_IDENT)
      return;
    out->id = oakc_intern_type_tok(c, elem->token);
    out->kind = OAK_TYPE_KIND_ARRAY;
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_MAP)
  {
    const struct oak_ast_node_t* key = type_node->lhs;
    const struct oak_ast_node_t* val = type_node->rhs;
    if (!key || !val || key->kind != OAK_NODE_IDENT ||
        val->kind != OAK_NODE_IDENT)
      return;
    out->key_id = oakc_intern_type_tok(c, key->token);
    out->id = oakc_intern_type_tok(c, val->token);
    out->kind = OAK_TYPE_KIND_MAP;
    return;
  }
}

int oakc_type_accepts(const struct oak_type_t* want,
                      const struct oak_type_t* got)
{
  if (oak_type_equal(want, got))
    return 1;
  if (want->is_weak && !got->is_weak && oak_type_equal_base(want, got))
    return 1;
  return 0;
}

oak_type_id_t oakc_intern_type_tok(struct oak_compiler_t* c,
                                             const struct oak_token_t* token)
{
  return oak_type_registry_intern(
      &c->types, oak_token_text(token), oak_token_length(token));
}

int oakc_local_type_get(struct oak_compiler_t* c,
                                const char* name,
                                const usize len,
                                struct oak_type_t* out)
{
  for (int i = c->scope.local_count - 1; i >= 0; --i)
  {
    const struct oak_local_t* L = &c->scope.locals[i];
    if (oak_name_eq(L->name, L->length, name, len))
    {
      *out = L->type;
      return 1;
    }
  }
  return 0;
}
