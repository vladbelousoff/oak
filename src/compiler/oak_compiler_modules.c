#include "oak_compiler_modules.h"

#include "oak_compiler_internal.h"
#include "oak_htable.h"
#include "oak_token.h"

const struct oak_module_t*
oak_compiler_module_for_alias(const struct oak_compiler_t* c,
                              const char* name,
                              usize name_len)
{
  if (!c->current_module || !c->module_registry)
    return null;
  const int mod_id =
      oak_htable_get(&c->current_module->imports, name, name_len);
  if (mod_id < 0)
    return null;
  return oak_module_registry_get(c->module_registry, (u16)mod_id);
}

const struct oak_module_export_fn_t*
oak_compiler_module_export_fn(const struct oak_compiler_t* c,
                              const char* alias,
                              usize alias_len,
                              const char* fn_name,
                              usize fn_name_len,
                              const struct oak_module_t** out_mod)
{
  const struct oak_module_t* mod =
      oak_compiler_module_for_alias(c, alias, alias_len);
  if (out_mod)
    *out_mod = mod;
  if (!mod)
    return null;
  return oak_module_find_export_fn(mod, fn_name, fn_name_len);
}

const struct oak_module_t*
oak_compiler_match_module_member(const struct oak_compiler_t* c,
                                 const struct oak_ast_node_t* node,
                                 const struct oak_token_t** out_member)
{
  if (!node || node->kind != OAK_NODE_MEMBER_ACCESS)
    return null;
  const struct oak_ast_node_t* lhs = node->lhs;
  const struct oak_ast_node_t* rhs = node->rhs;
  if (!lhs || !rhs || lhs->kind != OAK_NODE_IDENT || rhs->kind != OAK_NODE_IDENT)
    return null;
  const struct oak_module_t* mod = oak_compiler_module_for_alias(
      c, oak_token_text(lhs->token), oak_token_length(lhs->token));
  if (!mod)
    return null;
  if (out_member)
    *out_member = rhs->token;
  return mod;
}

int oak_compiler_import_path_segments(const struct oak_ast_node_t* path_node,
                                      const struct oak_ast_node_t** out_segs,
                                      int cap)
{
  int count = 0;
  if (!path_node)
    return 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &path_node->children)
  {
    const struct oak_ast_node_t* s =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (count < cap)
      out_segs[count] = s;
    ++count;
  }
  return count;
}
