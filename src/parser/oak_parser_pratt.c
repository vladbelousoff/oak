#include "oak_parser_internal.h"

static const struct oak_pratt_rule_t*
find_pratt_rule(const struct oak_pratt_rule_t* rules,
                const enum oak_token_kind_t token_kind)
{
  for (const struct oak_pratt_rule_t* r = rules; r->kind != OAK_PRATT_END; r++)
  {
    if (token_kind == r->trigger_token)
      return r;
  }
  return null;
}

static struct oak_ast_node_t*
parse_pratt_unary(struct oak_parser_t* p,
                  const enum oak_node_kind_t kind,
                  const struct oak_pratt_rule_t* rule)
{
  struct oak_ast_node_t* operand = oak_parser_parse_pratt(p, kind, rule->r_bp);
  if (!operand)
    return null;
  struct oak_ast_node_t* node =
      oak_arena_alloc(p->arena, sizeof(struct oak_ast_node_t));
  if (!node)
    return null;
  node->kind = rule->node_kind;
  node->child = operand;
  return node;
}

static struct oak_ast_node_t*
parse_pratt_binary(struct oak_parser_t* p,
                   const enum oak_node_kind_t kind,
                   const struct oak_pratt_rule_t* rule,
                   struct oak_ast_node_t* lhs)
{
  struct oak_ast_node_t* rhs = oak_parser_parse_pratt(p, kind, rule->r_bp);
  if (!rhs)
    return null;
  struct oak_ast_node_t* node =
      oak_arena_alloc(p->arena, sizeof(struct oak_ast_node_t));
  if (!node)
    return null;
  node->kind = rule->node_kind;
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

struct oak_ast_node_t* oak_parser_parse_pratt(struct oak_parser_t* p,
                                              const enum oak_node_kind_t kind,
                                              const int min_bp)
{
  const struct oak_grammar_entry_t* entry = &oak_grammar[kind];
  struct oak_ast_node_t* lhs = null;

  if (p->curr != p->head && entry->pratt.prefix)
  {
    const struct oak_token_t* token =
        oak_container_of(p->curr, struct oak_token_t, link);
    const struct oak_pratt_rule_t* r =
        find_pratt_rule(entry->pratt.prefix, oak_token_kind(token));
    if (r)
    {
      p->curr = p->curr->next;
      switch (r->kind)
      {
        case OAK_PRATT_GROUP:
          lhs = oak_parser_parse_pratt(p, kind, 0);
          if (!lhs || !oak_parser_try_skip_token(p, r->close_token))
            return null;
          break;
        default:
          lhs = parse_pratt_unary(p, kind, r);
          if (!lhs)
            return null;
          break;
      }
    }
  }

  if (!lhs)
  {
    lhs = oak_parser_parse_rule(p, entry->pratt.primary_rule);
    if (!lhs)
      return null;
  }

  for (;;)
  {
    if (p->curr == p->head)
      break;
    const struct oak_token_t* token =
        oak_container_of(p->curr, struct oak_token_t, link);

    const struct oak_pratt_rule_t* rule =
        find_pratt_rule(entry->pratt.infix, oak_token_kind(token));
    if (!rule || rule->l_bp < min_bp)
      break;

    p->curr = p->curr->next;

    switch (rule->kind)
    {
      case OAK_PRATT_CALL:
      {
        struct oak_ast_node_t* call =
            oak_arena_alloc(p->arena, sizeof(struct oak_ast_node_t));
        if (!call)
          return null;
        call->kind = rule->node_kind;
        oak_list_init(&call->children);
        oak_list_add_tail(&call->children, &lhs->link);

        while (p->curr != p->head)
        {
          const struct oak_token_t* peek =
              oak_container_of(p->curr, struct oak_token_t, link);
          if (oak_token_kind(peek) == rule->close_token)
            break;
          struct oak_ast_node_t* arg =
              oak_parser_parse_rule(p, rule->arg_rule);
          if (!arg)
            return null;
          oak_list_add_tail(&call->children, &arg->link);
        }

        if (!oak_parser_try_skip_token(p, rule->close_token))
          return null;

        lhs = call;
        break;
      }
      case OAK_PRATT_INDEX:
      {
        struct oak_ast_node_t* index_expr = oak_parser_parse_pratt(p, kind, 0);
        if (!index_expr)
          return null;
        if (!oak_parser_try_skip_token(p, rule->close_token))
          return null;
        struct oak_ast_node_t* node =
            oak_arena_alloc(p->arena, sizeof(struct oak_ast_node_t));
        if (!node)
          return null;
        node->kind = rule->node_kind;
        node->lhs = lhs;
        node->rhs = index_expr;
        lhs = node;
        break;
      }
      case OAK_PRATT_CAST:
      {
        struct oak_ast_node_t* type_node =
            oak_parser_parse_rule(p, OAK_NODE_TYPE_NAME);
        if (!type_node)
          return null;
        struct oak_ast_node_t* node =
            oak_arena_alloc(p->arena, sizeof(struct oak_ast_node_t));
        if (!node)
          return null;
        node->kind = rule->node_kind;
        node->lhs = lhs;
        node->rhs = type_node;
        lhs = node;
        break;
      }
      default:
        lhs = parse_pratt_binary(p, kind, rule, lhs);
        if (!lhs)
          return null;
        break;
    }
  }

  return lhs;
}
