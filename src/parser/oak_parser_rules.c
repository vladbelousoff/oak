#include "oak_parser_internal.h"

int oak_parser_try_skip_token(struct oak_parser_t* p,
                              const enum oak_token_kind_t token_kind)
{
  if (p->curr == p->head)
    return 0;
  const struct oak_token_t* token =
      oak_container_of(p->curr, struct oak_token_t, link);
  if (oak_token_kind(token) != token_kind)
    return 0;
  p->curr = p->curr->next;
  return 1;
}

usize oak_parser_grammar_rule_count(const struct oak_grammar_entry_t* entry)
{
  usize n = 0;
  while (n < oak_count_of(entry->rules) && entry->rules[n] != 0)
    ++n;
  return n;
}

struct oak_ast_node_t* oak_parser_parse_token(struct oak_parser_t* p,
                                              const enum oak_node_kind_t kind)
{
  oak_assert(p);
  oak_assert(p->curr);
  oak_assert(p->curr->next);
  const struct oak_token_t* token =
      oak_container_of(p->curr, struct oak_token_t, link);
  const struct oak_grammar_entry_t* entry = &oak_grammar[kind];
  if (oak_token_kind(token) != entry->token_kind)
    return null;
  struct oak_ast_node_t* node =
      oak_arena_alloc(p->arena, sizeof(struct oak_ast_node_t));
  if (!node)
    return null;
  node->kind = kind;
  node->token = token;
  p->curr = p->curr->next;
  return node;
}

struct oak_ast_node_t* oak_parser_parse_rules(struct oak_parser_t* p,
                                              const enum oak_node_kind_t kind)
{
  struct oak_list_entry_t* saved = p->curr;
  const struct oak_grammar_entry_t* entry = &oak_grammar[kind];
  struct oak_list_entry_t collected;
  oak_list_init(&collected);

  const usize count = oak_parser_grammar_rule_count(entry);
  for (usize i = 0; i < count; ++i)
  {
    const unsigned short rule = entry->rules[i];
    const int is_repeat = rule & OAK_RULE_REPEAT;
    const int is_optional = rule & OAK_RULE_OPTIONAL;

    if (rule & OAK_RULE_TOKEN)
    {
      const enum oak_token_kind_t tok =
          (enum oak_token_kind_t)(rule & OAK_RULE_KIND_MASK);
      if (is_repeat)
      {
        while (oak_parser_try_skip_token(p, tok))
        {
        }
        continue;
      }
      if (is_optional)
      {
        oak_parser_try_skip_token(p, tok);
        continue;
      }
      if (!oak_parser_try_skip_token(p, tok))
      {
        p->curr = saved;
        return null;
      }
      continue;
    }

    const enum oak_node_kind_t child_kind =
        (enum oak_node_kind_t)(rule & OAK_RULE_KIND_MASK);
    if (is_repeat)
    {
      for (;;)
      {
        struct oak_ast_node_t* child = oak_parser_parse_rule(p, child_kind);
        if (!child)
          break;
        oak_list_add_tail(&collected, &child->link);
      }
      continue;
    }

    struct oak_ast_node_t* child = oak_parser_parse_rule(p, child_kind);
    if (is_optional)
    {
      if (child)
        oak_list_add_tail(&collected, &child->link);
      continue;
    }
    if (!child)
    {
      p->curr = saved;
      return null;
    }
    oak_list_add_tail(&collected, &child->link);
  }

  const int is_binary = entry->op == OAK_GRAMMAR_BINARY;
  const int is_unary = entry->op == OAK_GRAMMAR_UNARY;
  if (is_binary && oak_list_length(&collected) < 2)
  {
    p->curr = saved;
    return null;
  }
  if (is_unary && oak_list_empty(&collected))
  {
    p->curr = saved;
    return null;
  }

  struct oak_ast_node_t* node =
      oak_arena_alloc(p->arena, sizeof(struct oak_ast_node_t));
  if (!node)
  {
    p->curr = saved;
    return null;
  }
  node->kind = kind;

  if (is_binary)
  {
    struct oak_list_entry_t* lhs = oak_list_first(&collected);
    struct oak_list_entry_t* rhs = oak_list_next(lhs, &collected);
    node->lhs = oak_container_of(lhs, struct oak_ast_node_t, link);
    node->rhs = oak_container_of(rhs, struct oak_ast_node_t, link);
  }
  else if (is_unary)
  {
    struct oak_list_entry_t* first = oak_list_first(&collected);
    node->child = oak_container_of(first, struct oak_ast_node_t, link);
  }
  else
  {
    oak_list_move(&collected, &node->children);
  }

  return node;
}
