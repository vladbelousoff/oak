#pragma once

#include "oak_arena.h"
#include "oak_count_of.h"
#include "oak_parser.h"

struct oak_parser_t
{
  const struct oak_list_entry_t* head;
  struct oak_list_entry_t* curr;
  struct oak_arena_t* arena;
};

#define OAK_RULE_TOKEN     ((unsigned short)(1 << 15))
#define OAK_RULE_REPEAT    ((unsigned short)(1 << 14))
#define OAK_RULE_OPTIONAL  ((unsigned short)(1 << 13))
/* When combined with OAK_RULE_REPEAT (non-token), skip an optional comma
 * before each repetition attempt.  Handles both space- and comma-separated
 * lists and allows an optional trailing comma. */
#define OAK_RULE_COMMA_SEP ((unsigned short)(1 << 12))
#define OAK_RULE_KIND_MASK                                                     \
  ((unsigned short)~(OAK_RULE_TOKEN | OAK_RULE_REPEAT |                       \
                     OAK_RULE_OPTIONAL | OAK_RULE_COMMA_SEP))

enum oak_grammar_op_t
{
  OAK_GRAMMAR_SEQUENCE, // Match all children in order (default)
  OAK_GRAMMAR_TOKEN,    // Match one specific token (terminal)
  OAK_GRAMMAR_CHOICE,   // Match first succeeding child (A | B | C)
  OAK_GRAMMAR_PRATT,    // Pratt parser for operator precedence
  OAK_GRAMMAR_BINARY,   // Produce binary node (lhs/rhs)
  OAK_GRAMMAR_UNARY,    // Produce unary node (single child)
};

enum oak_pratt_op_t
{
  OAK_PRATT_END,
  OAK_PRATT_OP,
  OAK_PRATT_GROUP,
  OAK_PRATT_CALL,
  OAK_PRATT_INDEX,
  OAK_PRATT_CAST,
};

struct oak_pratt_rule_t
{
  enum oak_pratt_op_t kind;
  enum oak_token_kind_t trigger_token;
  int l_bp;
  int r_bp;
  enum oak_node_kind_t node_kind;
  enum oak_token_kind_t close_token;
  enum oak_node_kind_t arg_rule;
};

struct oak_grammar_entry_t
{
  enum oak_grammar_op_t op;
  union
  {
    unsigned short rules[16];
    enum oak_token_kind_t token_kind;
    struct
    {
      enum oak_node_kind_t primary_rule;
      const struct oak_pratt_rule_t* prefix;
      const struct oak_pratt_rule_t* infix;
    } pratt;
  };
};

extern struct oak_grammar_entry_t oak_grammar[];

/* Top-level dispatcher (defined in oak_parser.c). */
struct oak_ast_node_t* oak_parser_parse_rule(struct oak_parser_t* p,
                                             enum oak_node_kind_t kind);

/* Sub-parsers (one per grammar op). */
struct oak_ast_node_t* oak_parser_parse_token(struct oak_parser_t* p,
                                              enum oak_node_kind_t kind);

struct oak_ast_node_t* oak_parser_parse_rules(struct oak_parser_t* p,
                                              enum oak_node_kind_t kind);

struct oak_ast_node_t* oak_parser_parse_choice(struct oak_parser_t* p,
                                               enum oak_node_kind_t kind);

struct oak_ast_node_t* oak_parser_parse_pratt(struct oak_parser_t* p,
                                              enum oak_node_kind_t kind,
                                              int min_bp);

/* Shared helpers. */
int oak_parser_try_skip_token(struct oak_parser_t* p,
                              enum oak_token_kind_t token_kind);

usize oak_parser_grammar_rule_count(const struct oak_grammar_entry_t* entry);
