#pragma once

#include "oak_lex.h"
#include "oak_tok.h"

typedef enum
{
  OAK_PARSER_RULE_NONE,
  OAK_PARSER_RULE_PROGRAM,
  OAK_PARSER_RULE_PROGRAM_ITEM,
  OAK_PARSER_RULE_TYPE_DECL,
  OAK_PARSER_RULE_TYPE_KEYWORD,
  OAK_PARSER_RULE_TYPE_NAME,
  OAK_PARSER_RULE_TYPE_FIELD_DECLS,
  OAK_PARSER_RULE_TYPE_FIELD_DECL,
  OAK_PARSER_RULE_LBRACE,
  OAK_PARSER_RULE_RBRACE,
  OAK_PARSER_RULE_IDENT,
  OAK_PARSER_RULE_COLON,
  OAK_PARSER_RULE_SEMICOLON,
} oak_parser_rule_id_t;

typedef struct _oak_ast_node_t
{
  oak_list_entry_t link;
  oak_parser_rule_id_t rule_id;

  union
  {
    const oak_tok_t* tok;
    oak_list_head_t children;
  };
} oak_ast_node_t;

oak_ast_node_t* oak_parse(const oak_lex_t* lex, oak_parser_rule_id_t rule_id);
