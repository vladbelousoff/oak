#include "internal/oak_compiler.h"

static const struct oak_ast_node_t*
oak_fn_decl_proto(const struct oak_ast_node_t* decl)
{
  return decl->lhs;
}

static const struct oak_ast_node_t*
oak_fn_decl_head(const struct oak_ast_node_t* decl)
{
  return oak_fn_decl_proto(decl)->lhs;
}

static const struct oak_ast_node_t*
oak_fn_decl_params_tail(const struct oak_ast_node_t* decl)
{
  return oak_fn_decl_proto(decl)->rhs;
}

static const struct oak_ast_node_t*
oak_fn_param_list_regular_params(const struct oak_ast_node_t* plist)
{
  /* FN_PARAM_LIST is BINARY: lhs = FN_PARAM_SELF?, rhs = FN_PARAMS. */
  return plist->rhs;
}

const struct oak_ast_node_t*
oakc_fn_param_list(const struct oak_ast_node_t* decl)
{
  /* FN_PARAMS_AND_RET is BINARY: lhs = FN_PARAM_LIST, rhs = FN_RETURN_TYPE?. */
  return oak_fn_decl_params_tail(decl)->lhs;
}

const struct oak_ast_node_t*
oakc_fn_name_node(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* head = oak_fn_decl_head(decl);
  oak_assert(head->rhs != null);
  return head->rhs;
}

const struct oak_ast_node_t*
oakc_fn_self_param(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = oakc_fn_param_list(decl);
  if (!plist)
    return null;
  /* FN_PARAM_LIST is BINARY: lhs = FN_PARAM_SELF? (null when absent). */
  return plist->lhs;
}

int oakc_self_is_mut(
    const struct oak_ast_node_t* self_param)
{
  /* FN_PARAM_SELF is BINARY: lhs = MUT_KEYWORD? (non-null iff mutable). */
  return self_param->lhs != null;
}

const struct oak_ast_node_t*
oakc_fn_block(const struct oak_ast_node_t* decl)
{
  return (decl->rhs && decl->rhs->kind == OAK_NODE_BLOCK) ? decl->rhs : null;
}

int oakc_param_is_mut(const struct oak_ast_node_t* param)
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

const struct oak_ast_node_t*
oakc_fn_param_ident(const struct oak_ast_node_t* param)
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

const struct oak_ast_node_t*
oakc_fn_param_type_node(const struct oak_ast_node_t* param)
{
  int ident_seen = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_MUT_KEYWORD)
      continue;
    if (ch->kind == OAK_NODE_IDENT)
    {
      if (!ident_seen)
      {
        ident_seen = 1;
        continue;
      }
      return ch;
    }
    if (ch->kind == OAK_NODE_TYPE_ARRAY || ch->kind == OAK_NODE_TYPE_MAP)
      return ch;
  }
  return null;
}

const struct oak_ast_node_t*
oakc_fn_param_at(const struct oak_ast_node_t* decl,
                              const int index)
{
  const struct oak_ast_node_t* plist = oakc_fn_param_list(decl);
  if (!plist)
    return null;
  const struct oak_ast_node_t* params = oak_fn_param_list_regular_params(plist);
  if (!params)
    return null;
  int i = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &params->children)
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

const struct oak_ast_node_t*
oakc_fn_return_type_node(const struct oak_ast_node_t* decl)
{
  /* FN_PARAMS_AND_RET is BINARY: rhs = FN_RETURN_TYPE? (null when absent).
   * FN_RETURN_TYPE is UNARY: child = TYPE_NAME. */
  const struct oak_ast_node_t* tail = oak_fn_decl_params_tail(decl);
  if (!tail->rhs)
    return null;
  return tail->rhs->child;
}

int oakc_count_fn_params(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = oakc_fn_param_list(decl);
  if (!plist)
    return 0;
  const struct oak_ast_node_t* params = oak_fn_param_list_regular_params(plist);
  if (!params)
    return 0;
  int n = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &params->children)
  {
    const struct oak_ast_node_t* ch =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM)
      ++n;
  }
  return n;
}
