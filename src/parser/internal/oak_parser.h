#pragma once

#include "oak_arena.h"
#include "oak_count_of.h"
#include <oak_parser.h>

/* The concrete parser result. Opaque in the public header: the nodes live in
 * the arena below, so the struct is only meaningful to the parser itself and
 * to the module loader, which keeps one per module. */
struct oak_parser_result
{
  oak_ast_node_t* root;
  oak_arena_t arena;
  oak_allocator_t* allocator;
  oak_diagnostic_t errors[OAK_MAX_DIAGNOSTICS];
  int error_count;
};

typedef struct oak_parser oak_parser_t;
struct oak_parser
{
  const oak_list_entry_t* head;
  oak_list_entry_t* curr;
  oak_arena_t* arena;
  int detail_valid;
  const oak_token_t* detail_token;
  int detail_has_expected_token;
  oak_token_kind_t detail_expected_token;
  oak_node_kind_t detail_expected_node;
  oak_node_kind_t detail_context;
};

#define OAK_RULE_TOKEN    ((unsigned short)(1 << 15))
#define OAK_RULE_REPEAT   ((unsigned short)(1 << 14))
#define OAK_RULE_OPTIONAL ((unsigned short)(1 << 13))
/* When combined with OAK_RULE_REPEAT (non-token), skip an optional comma
 * before each repetition attempt.  Handles both space- and comma-separated
 * lists and allows an optional trailing comma. */
#define OAK_RULE_COMMA_SEP ((unsigned short)(1 << 12))
/* When combined with OAK_RULE_REPEAT (non-token), require a DOT between
 * elements (no leading or trailing dot).  Used by import paths a.b.c. */
#define OAK_RULE_DOT_SEP ((unsigned short)(1 << 11))
#define OAK_RULE_KIND_MASK                                                     \
  ((unsigned short)~(OAK_RULE_TOKEN | OAK_RULE_REPEAT | OAK_RULE_OPTIONAL |    \
                     OAK_RULE_COMMA_SEP | OAK_RULE_DOT_SEP))

typedef enum oak_grammar_op oak_grammar_op_t;
enum oak_grammar_op
{
  OAK_GRAMMAR_SEQUENCE, // Match all children in order (default)
  OAK_GRAMMAR_TOKEN,    // Match one specific token (terminal)
  OAK_GRAMMAR_CHOICE,   // Match first succeeding child (A | B | C)
  OAK_GRAMMAR_PRATT,    // Pratt parser for operator precedence
  OAK_GRAMMAR_BINARY,   // Produce binary node (lhs/rhs)
  OAK_GRAMMAR_UNARY,    // Produce unary node (single child)
};

typedef enum oak_pratt_op oak_pratt_op_t;
enum oak_pratt_op
{
  OAK_PRATT_END,
  OAK_PRATT_OP,
  OAK_PRATT_GROUP,
  OAK_PRATT_CALL,
  OAK_PRATT_INDEX,
};

typedef struct oak_pratt_rule oak_pratt_rule_t;
struct oak_pratt_rule
{
  oak_pratt_op_t kind;
  oak_token_kind_t trigger_token;
  int l_bp;
  int r_bp;
  oak_node_kind_t node_kind;
  oak_token_kind_t close_token;
  oak_node_kind_t arg_rule;
};

typedef struct oak_grammar_entry oak_grammar_entry_t;
struct oak_grammar_entry
{
  oak_grammar_op_t op;
  union
  {
    unsigned short rules[16];
    oak_token_kind_t token_kind;
    struct
    {
      oak_node_kind_t primary_rule;
      const oak_pratt_rule_t* prefix;
      const oak_pratt_rule_t* infix;
    } pratt;
  };
};

extern oak_grammar_entry_t oak_grammar[];

/* Top-level dispatcher (defined in oak_parser.c). */
oak_ast_node_t* oak_parser_parse_rule(oak_parser_t* p,
                                             oak_node_kind_t kind);

/* Sub-parsers (one per grammar op). */
oak_ast_node_t* oak_parser_parse_token(oak_parser_t* p,
                                              oak_node_kind_t kind);

oak_ast_node_t* oak_parser_parse_rules(oak_parser_t* p,
                                              oak_node_kind_t kind);

oak_ast_node_t* oak_parser_parse_choice(oak_parser_t* p,
                                               oak_node_kind_t kind);

oak_ast_node_t* oak_parser_parse_pratt(oak_parser_t* p,
                                              oak_node_kind_t kind,
                                              int min_bp);

/* Shared helpers. */
int oak_parser_try_skip_token(oak_parser_t* p,
                              oak_token_kind_t token_kind);

usize oak_parser_grammar_rule_count(const oak_grammar_entry_t* entry);

void oak_parser_detail_expected_token(oak_parser_t* p,
                                      oak_node_kind_t context,
                                      oak_token_kind_t expected);

void oak_parser_detail_expected_node(oak_parser_t* p,
                                     oak_node_kind_t context,
                                     oak_node_kind_t expected);
