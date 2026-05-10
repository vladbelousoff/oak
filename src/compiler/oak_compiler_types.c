#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

void oc_lower_type_node(struct oak_compiler_t* c,
                                    const struct oak_ast_node_t* type_node,
                                    struct oak_type_t* out)
{
  oak_type_clear(out);
  if (!type_node)
    return;
  if (type_node->kind == OAK_NODE_IDENT)
  {
    out->id = oc_intern_type_tok(c, type_node->token);
    return;
  }
  if (type_node->kind == OAK_NODE_TYPE_ARRAY)
  {
    const struct oak_ast_node_t* elem = type_node->child;
    if (!elem || elem->kind != OAK_NODE_IDENT)
      return;
    out->id = oc_intern_type_tok(c, elem->token);
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
    out->key_id = oc_intern_type_tok(c, key->token);
    out->id = oc_intern_type_tok(c, val->token);
    out->kind = OAK_TYPE_KIND_MAP;
    return;
  }
}

oak_type_id_t oc_intern_type_tok(struct oak_compiler_t* c,
                                             const struct oak_token_t* token)
{
  return oak_type_registry_intern(
      &c->types, oak_token_text(token), oak_token_length(token));
}

int oc_local_type_get(struct oak_compiler_t* c,
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
