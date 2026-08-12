#include "oak_compiler_modules.h"

#include "internal/oak_compiler.h"
#include "oak_token.h"

const oak_module_t* oak_compiler_module_for_alias(
    const oak_compiler_t* c, const char* name)
{
  if (!c->current_module || !c->module_registry)
    return null;
  const usize* mod_id = oak_cfind_str(c->current_module->imports, name);
  if (!mod_id)
    return null;
  return oak_module_registry_get(c->module_registry, (u16)*mod_id);
}

const oak_module_export_fn_t*
oak_compiler_module_export_fn(const oak_compiler_t* c,
                              const char* alias,
                              const char* fn_name,
                              const oak_module_t** out_mod)
{
  const oak_module_t* mod = oak_compiler_module_for_alias(c, alias);
  if (out_mod)
    *out_mod = mod;
  if (!mod)
    return null;
  return oak_module_find_export_fn(mod, fn_name);
}

const oak_module_export_record_t*
oak_compiler_module_export_record(const oak_compiler_t* c,
                                  const char* alias,
                                  const char* type_name,
                                  const oak_module_t** out_mod)
{
  const oak_module_t* mod = oak_compiler_module_for_alias(c, alias);
  if (out_mod)
    *out_mod = mod;
  if (!mod)
    return null;
  return oak_module_find_export_record(mod, type_name);
}

const oak_module_export_enum_t*
oak_compiler_module_export_enum(const oak_compiler_t* c,
                                const char* alias,
                                const char* enum_name,
                                const oak_module_t** out_mod)
{
  const oak_module_t* mod = oak_compiler_module_for_alias(c, alias);
  if (out_mod)
    *out_mod = mod;
  if (!mod)
    return null;
  return oak_module_find_export_enum(mod, enum_name);
}

const oak_module_t*
oak_compiler_match_module_member(const oak_compiler_t* c,
                                 const oak_ast_node_t* node,
                                 const oak_token_t** out_member)
{
  if (!node || node->kind != OAK_NODE_MEMBER_ACCESS)
    return null;
  const oak_ast_node_t* lhs = node->lhs;
  const oak_ast_node_t* rhs = node->rhs;
  if (!lhs || !rhs || lhs->kind != OAK_NODE_IDENT ||
      rhs->kind != OAK_NODE_IDENT)
    return null;
  const oak_module_t* mod = oak_compiler_module_for_alias(c, oak_token_text(lhs->token));
  if (!mod)
    return null;
  if (out_member)
    *out_member = rhs->token;
  return mod;
}

int oak_compiler_import_path_segments(const oak_ast_node_t* path_node,
                                      const oak_ast_node_t** out_segs,
                                      int cap)
{
  int count = 0;
  if (!path_node)
    return 0;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &path_node->children)
  {
    const oak_ast_node_t* s =
        oak_container_of(pos, oak_ast_node_t, link);
    if (count < cap)
      out_segs[count] = s;
    ++count;
  }
  return count;
}
