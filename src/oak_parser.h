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

typedef enum
{
  OAK_PARSER_OP_TOKEN,      // Match one specific token (terminal)
  OAK_PARSER_OP_SEQUENCE,   // Match all children in order (A B C)
  OAK_PARSER_OP_CHOICE,     // Match first succeeding child (A | B | C)
  OAK_PARSER_OP_REPEAT,     // Match child zero or more times (A*)
  OAK_PARSER_OP_REPEAT_ONE, // Match child one or more times (A+)
  OAK_PARSER_OP_OPTIONAL,   // Match child zero or one times (A?)
} oak_parser_rule_op_t;

typedef struct
{
  oak_parser_rule_op_t op;
  union
  {
    oak_parser_rule_id_t rules[16];
    oak_tok_type_t tok_type;
  };
} oak_parser_rule_t;

extern oak_parser_rule_t oak_grammar[];

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
void oak_ast_node_cleanup(oak_ast_node_t* node);
