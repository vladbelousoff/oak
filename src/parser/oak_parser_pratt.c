#include "internal/oak_parser.h"

static const oak_pratt_rule_t*
find_pratt_rule(const oak_pratt_rule_t* rules,
                const oak_token_kind_t token_kind)
{
  for (const oak_pratt_rule_t* r = rules; r->kind != OAK_PRATT_END; r++)
  {
    if (token_kind == r->trigger_token)
      return r;
  }
  return OAK_NULL;
}

static oak_ast_node_t*
parse_pratt_unary(oak_parser_t* p,
                  const oak_node_kind_t kind,
                  const oak_pratt_rule_t* rule)
{
  oak_ast_node_t* operand = oak_parser_parse_pratt(p, kind, rule->r_bp);
  if (!operand)
  {
    oak_parser_detail_expected_node(p, rule->node_kind, kind);
    return OAK_NULL;
  }
  oak_ast_node_t* node =
      oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
  if (!node)
    return OAK_NULL;
  node->kind = rule->node_kind;
  node->child = operand;
  return node;
}

static oak_ast_node_t*
parse_pratt_binary(oak_parser_t* p,
                   const oak_node_kind_t kind,
                   const oak_pratt_rule_t* rule,
                   oak_ast_node_t* lhs)
{
  oak_ast_node_t* rhs = oak_parser_parse_pratt(p, kind, rule->r_bp);
  if (!rhs)
  {
    oak_parser_detail_expected_node(p, rule->node_kind, kind);
    return OAK_NULL;
  }
  oak_ast_node_t* node =
      oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
  if (!node)
    return OAK_NULL;
  node->kind = rule->node_kind;
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

oak_ast_node_t* oak_parser_parse_pratt(oak_parser_t* p,
                                              const oak_node_kind_t kind,
                                              const int min_bp)
{
  const oak_grammar_entry_t* entry = &oak_grammar[kind];
  oak_ast_node_t* lhs = OAK_NULL;

  if (p->curr != p->head && entry->pratt.prefix)
  {
    const oak_token_t* token =
        OAK_CONTAINER_OF(p->curr, oak_token_t, link);
    const oak_pratt_rule_t* r =
        find_pratt_rule(entry->pratt.prefix, oak_token_kind(token));
    if (r)
    {
      oak_list_entry_t* saved = p->curr;
      p->curr = p->curr->next;
      switch (r->kind)
      {
        case OAK_PRATT_GROUP:
          /* A '(' does not have to open a grouped expression: it also opens an
           * anonymous function's parameter list. Rather than look ahead, fall
           * back to the primary rule -- which offers EXPR_LAMBDA -- the way
           * oak_parser_parse_choice retries its alternatives. The failure
           * detail is still recorded, and oak_parser_should_replace_detail
           * keeps whichever attempt got furthest, so a genuinely malformed
           * '(1 +' still reports the error from inside the parens. */
          lhs = oak_parser_parse_pratt(p, kind, 0);
          if (!lhs)
          {
            oak_parser_detail_expected_node(p, kind, kind);
            p->curr = saved;
            break;
          }
          if (!oak_parser_try_skip_token(p, r->close_token))
          {
            oak_parser_detail_expected_token(p, kind, r->close_token);
            lhs = OAK_NULL;
            p->curr = saved;
            break;
          }
          break;
        default:
          lhs = parse_pratt_unary(p, kind, r);
          if (!lhs)
            return OAK_NULL;
          break;
      }
    }
  }

  if (!lhs)
  {
    lhs = oak_parser_parse_rule(p, entry->pratt.primary_rule);
    if (!lhs)
    {
      oak_parser_detail_expected_node(p, kind, entry->pratt.primary_rule);
      return OAK_NULL;
    }
  }

  for (;;)
  {
    if (p->curr == p->head)
      break;
    const oak_token_t* token =
        OAK_CONTAINER_OF(p->curr, oak_token_t, link);

    const oak_pratt_rule_t* rule =
        find_pratt_rule(entry->pratt.infix, oak_token_kind(token));
    if (!rule || rule->l_bp < min_bp)
      break;

    p->curr = p->curr->next;

    switch (rule->kind)
    {
      case OAK_PRATT_CALL:
      {
        oak_ast_node_t* call =
            oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
        if (!call)
          return OAK_NULL;
        call->kind = rule->node_kind;
        oak_list_init(&call->children);
        oak_list_add_tail(&call->children, &lhs->link);

        while (p->curr != p->head)
        {
          const oak_token_t* peek =
              OAK_CONTAINER_OF(p->curr, oak_token_t, link);
          if (oak_token_kind(peek) == rule->close_token)
            break;
          oak_ast_node_t* arg = oak_parser_parse_rule(p, rule->arg_rule);
          if (!arg)
          {
            oak_parser_detail_expected_node(p, rule->node_kind, rule->arg_rule);
            return OAK_NULL;
          }
          oak_list_add_tail(&call->children, &arg->link);
        }

        if (!oak_parser_try_skip_token(p, rule->close_token))
        {
          oak_parser_detail_expected_token(p, rule->node_kind, rule->close_token);
          return OAK_NULL;
        }

        lhs = call;
        break;
      }
      case OAK_PRATT_INDEX:
      {
        oak_ast_node_t* index_expr = oak_parser_parse_pratt(p, kind, 0);
        if (!index_expr)
        {
          oak_parser_detail_expected_node(p, rule->node_kind, kind);
          return OAK_NULL;
        }
        if (!oak_parser_try_skip_token(p, rule->close_token))
        {
          oak_parser_detail_expected_token(p, rule->node_kind, rule->close_token);
          return OAK_NULL;
        }
        oak_ast_node_t* node =
            oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
        if (!node)
          return OAK_NULL;
        node->kind = rule->node_kind;
        node->lhs = lhs;
        node->rhs = index_expr;
        lhs = node;
        break;
      }
      default:
        lhs = parse_pratt_binary(p, kind, rule, lhs);
        if (!lhs)
          return OAK_NULL;
        break;
    }
  }

  return lhs;
}
