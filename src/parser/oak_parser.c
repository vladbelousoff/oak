#include "oak_parser_internal.h"

#include <stdio.h>

struct oak_ast_node_t* oak_parser_parse_rule(struct oak_parser_t* p,
                                             const enum oak_node_kind_t kind)
{
  if (kind == OAK_NODE_NONE)
    return null;
  if (p->curr == p->head)
    return null;

  switch (oak_grammar[kind].op)
  {
    case OAK_GRAMMAR_TOKEN:
      return oak_parser_parse_token(p, kind);
    case OAK_GRAMMAR_CHOICE:
      return oak_parser_parse_choice(p, kind);
    case OAK_GRAMMAR_PRATT:
      return oak_parser_parse_pratt(p, kind, 0);
    case OAK_GRAMMAR_BINARY:
    case OAK_GRAMMAR_UNARY:
    case OAK_GRAMMAR_SEQUENCE:
      return oak_parser_parse_rules(p, kind);
  }
  return null;
}

void oak_parse(const struct oak_lexer_result_t* lexer,
               const enum oak_node_kind_t kind,
               struct oak_parser_result_t* out)
{
  oak_arena_init(&out->arena, 0);

  const struct oak_list_entry_t* tokens = oak_lexer_tokens(lexer);
  struct oak_parser_t parser = {
    .head = tokens,
    .curr = tokens->next,
    .arena = &out->arena,
  };

  out->root = oak_parser_parse_rule(&parser, kind);

  if (parser.curr != parser.head)
  {
    const struct oak_token_t* token =
        oak_container_of(parser.curr, struct oak_token_t, link);
    if (out->error_count < OAK_MAX_DIAGNOSTICS)
    {
      struct oak_diagnostic_t* d = &out->errors[out->error_count++];
      d->line = oak_token_line(token);
      d->column = oak_token_column(token);
      snprintf(d->message,
               sizeof(d->message),
               "unexpected token '%s'",
               oak_token_kind(token) != OAK_TOKEN_IDENT
                   ? oak_token_name(oak_token_kind(token))
                   : oak_token_text(token));
    }
    out->root = null;
  }
}

struct oak_ast_node_t* oak_parser_root(const struct oak_parser_result_t* result)
{
  return result ? result->root : null;
}

int oak_parser_error_count(const struct oak_parser_result_t* result)
{
  return result ? result->error_count : 0;
}

const struct oak_diagnostic_t*
oak_parser_errors(const struct oak_parser_result_t* result)
{
  return result ? result->errors : null;
}

void oak_parser_free(struct oak_parser_result_t* result)
{
  if (!result)
    return;
  oak_arena_free(&result->arena);
}
