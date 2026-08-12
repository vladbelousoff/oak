#include "internal/oak_compiler.h"

static const oak_ast_node_t*
oak_fn_decl_proto(const oak_ast_node_t* decl)
{
  return decl->lhs;
}

static const oak_ast_node_t*
oak_fn_decl_head(const oak_ast_node_t* decl)
{
  return oak_fn_decl_proto(decl)->lhs;
}

static const oak_ast_node_t*
oak_fn_decl_params_tail(const oak_ast_node_t* decl)
{
  if (decl->kind == OAK_NODE_EXPR_FN)
    return decl->lhs;
  return oak_fn_decl_proto(decl)->rhs;
}

static const oak_ast_node_t*
oak_fn_param_list_regular_params(const oak_ast_node_t* plist)
{
  /* FN_PARAM_LIST is BINARY: lhs = FN_PARAM_SELF?, rhs = FN_PARAMS. */
  return plist->rhs;
}

const oak_ast_node_t*
oak_fn_param_list(const oak_ast_node_t* decl)
{
  /* FN_PARAMS_AND_RET is BINARY: lhs = FN_PARAM_LIST, rhs = FN_RETURN_TYPE?. */
  return oak_fn_decl_params_tail(decl)->lhs;
}

const oak_ast_node_t*
oak_fn_name_node(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* head = oak_fn_decl_head(decl);
  oak_assert(head->rhs != null);
  return head->rhs;
}

const oak_ast_node_t*
oak_fn_self_param(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* plist = oak_fn_param_list(decl);
  if (!plist)
    return null;
  /* FN_PARAM_LIST is BINARY: lhs = FN_PARAM_SELF? (null when absent). */
  return plist->lhs;
}

int oak_self_is_mut(
    const oak_ast_node_t* self_param)
{
  /* FN_PARAM_SELF is BINARY: lhs = MUT_KEYWORD? (non-null iff mutable). */
  return self_param->lhs != null;
}

const oak_ast_node_t*
oak_fn_block(const oak_ast_node_t* decl)
{
  return (decl->rhs && decl->rhs->kind == OAK_NODE_BLOCK) ? decl->rhs : null;
}

int oak_param_is_mut(const oak_ast_node_t* param)
{
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const oak_ast_node_t* ch =
        oak_container_of(pos, oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_MUT_KEYWORD)
      return 1;
  }
  return 0;
}

const oak_ast_node_t*
oak_fn_param_ident(const oak_ast_node_t* param)
{
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const oak_ast_node_t* ch =
        oak_container_of(pos, oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_IDENT)
      return ch;
  }
  return null;
}

const oak_ast_node_t*
oak_fn_param_type_node(const oak_ast_node_t* param)
{
  int ident_seen = 0;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &param->children)
  {
    const oak_ast_node_t* ch =
        oak_container_of(pos, oak_ast_node_t, link);
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
    if (ch->kind == OAK_NODE_TYPE_ARRAY || ch->kind == OAK_NODE_TYPE_MAP ||
        ch->kind == OAK_NODE_TYPE_WEAK || ch->kind == OAK_NODE_TYPE_FN)
      return ch;
  }
  return null;
}

const oak_ast_node_t*
oak_fn_param_at(const oak_ast_node_t* decl,
                              const int index)
{
  const oak_ast_node_t* plist = oak_fn_param_list(decl);
  if (!plist)
    return null;
  const oak_ast_node_t* params = oak_fn_param_list_regular_params(plist);
  if (!params)
    return null;
  int i = 0;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &params->children)
  {
    const oak_ast_node_t* ch =
        oak_container_of(pos, oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM)
    {
      if (i == index)
        return ch;
      ++i;
    }
  }
  return null;
}

const oak_ast_node_t*
oak_fn_return_type_node(const oak_ast_node_t* decl)
{
  /* FN_PARAMS_AND_RET is BINARY: rhs = FN_RETURN_TYPE? (null when absent).
   * FN_RETURN_TYPE is UNARY: child = TYPE_NAME. */
  const oak_ast_node_t* tail = oak_fn_decl_params_tail(decl);
  if (!tail->rhs)
    return null;
  return tail->rhs->child;
}

int oak_count_fn_params(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* plist = oak_fn_param_list(decl);
  if (!plist)
    return 0;
  const oak_ast_node_t* params = oak_fn_param_list_regular_params(plist);
  if (!params)
    return 0;
  int n = 0;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &params->children)
  {
    const oak_ast_node_t* ch =
        oak_container_of(pos, oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM)
      ++n;
  }
  return n;
}
