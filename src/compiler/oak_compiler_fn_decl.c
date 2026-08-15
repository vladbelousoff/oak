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
  if (decl->kind == OAK_NODE_EXPR_LAMBDA)
    return decl->lhs;
  return oak_fn_decl_proto(decl)->rhs;
}

static const oak_ast_node_t*
oak_fn_param_list_regular_params(const oak_ast_node_t* plist)
{
  /* FN_PARAM_LIST is UNARY: child = FN_PARAMS. The receiver is not a
   * parameter — it lives on FN_PREFIX, see oak_fn_receiver_mode. */
  return plist->child;
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
  OAK_ASSERT(head->rhs != OAK_NULL);
  return head->rhs;
}

const oak_ast_node_t*
oak_fn_receiver_mode(const oak_ast_node_t* decl)
{
  /* EXPR_FN has no head at all — it is `fn` followed straight by the params,
   * so it can never carry a mode. */
  if (decl->kind == OAK_NODE_EXPR_LAMBDA)
    return OAK_NULL;
  const oak_ast_node_t* head = oak_fn_decl_head(decl);
  if (!head || !head->lhs)
    return OAK_NULL;
  /* FN_HEAD is BINARY: lhs = FN_PREFIX, which is UNARY with
   * child = FN_RECEIVER_MODE? (null for a plain `fn`). The mode is a
   * transparent choice, so the child is MUT_KEYWORD or STATIC_KEYWORD. */
  return head->lhs->child;
}

int oak_fn_is_static(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* mode = oak_fn_receiver_mode(decl);
  return mode != OAK_NULL && mode->kind == OAK_NODE_STATIC_KEYWORD;
}

int oak_fn_self_is_mut(const oak_ast_node_t* decl)
{
  const oak_ast_node_t* mode = oak_fn_receiver_mode(decl);
  return mode != OAK_NULL && mode->kind == OAK_NODE_MUT_KEYWORD;
}

const oak_ast_node_t*
oak_fn_block(const oak_ast_node_t* decl)
{
  return (decl->rhs && decl->rhs->kind == OAK_NODE_BLOCK) ? decl->rhs : OAK_NULL;
}

int oak_param_is_mut(const oak_ast_node_t* param)
{
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &param->children)
  {
    const oak_ast_node_t* ch =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_MUT_KEYWORD)
      return 1;
  }
  return 0;
}

const oak_ast_node_t*
oak_fn_param_ident(const oak_ast_node_t* param)
{
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &param->children)
  {
    const oak_ast_node_t* ch =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_IDENT)
      return ch;
  }
  return OAK_NULL;
}

const oak_ast_node_t*
oak_fn_param_type_node(const oak_ast_node_t* param)
{
  int ident_seen = 0;
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &param->children)
  {
    const oak_ast_node_t* ch =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
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
  return OAK_NULL;
}

const oak_ast_node_t*
oak_fn_param_at(const oak_ast_node_t* decl,
                              const int index)
{
  const oak_ast_node_t* plist = oak_fn_param_list(decl);
  if (!plist)
    return OAK_NULL;
  const oak_ast_node_t* params = oak_fn_param_list_regular_params(plist);
  if (!params)
    return OAK_NULL;
  int i = 0;
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &params->children)
  {
    const oak_ast_node_t* ch =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM)
    {
      if (i == index)
        return ch;
      ++i;
    }
  }
  return OAK_NULL;
}

const oak_ast_node_t*
oak_fn_return_type_node(const oak_ast_node_t* decl)
{
  /* FN_PARAMS_AND_RET is BINARY: rhs = FN_RETURN_TYPE? (null when absent).
   * FN_RETURN_TYPE is UNARY: child = TYPE_NAME. */
  const oak_ast_node_t* tail = oak_fn_decl_params_tail(decl);
  if (!tail->rhs)
    return OAK_NULL;
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
  OAK_LIST_FOR_EACH(pos, &params->children)
  {
    const oak_ast_node_t* ch =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    if (ch->kind == OAK_NODE_FN_PARAM)
      ++n;
  }
  return n;
}
