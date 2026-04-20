#include "oak_parser_internal.h"

struct oak_ast_node_t* oak_parser_parse_choice(struct oak_parser_t* p,
                                               const enum oak_node_kind_t kind)
{
  struct oak_list_entry_t* saved = p->curr;
  const struct oak_grammar_entry_t* entry = &oak_grammar[kind];

  const usize choice_count = oak_parser_grammar_rule_count(entry);
  for (usize i = 0; i < choice_count; ++i)
  {
    struct oak_ast_node_t* child =
        oak_parser_parse_rule(p, (enum oak_node_kind_t)entry->rules[i]);
    if (child)
      return child;
  }

  p->curr = saved;
  return null;
}
