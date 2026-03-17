#include "oak_parser.h"
#include <stdio.h>

#include "oak_common.h"
#include "oak_log.h"
#include "oak_mem.h"

typedef struct
{
  const oak_list_head_t* head;
  oak_list_head_t* curr;
} oak_parser_t;

typedef int (*oak_parser_rule_fn_t)(oak_parser_t*);

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

static oak_parser_rule_t oak_grammar[] = {
  // PROGRAM -> (PROGRAM_ITEM)*
  [OAK_PARSER_RULE_PROGRAM] = {
    .op = OAK_PARSER_OP_REPEAT,
    .rules = {
      OAK_PARSER_RULE_PROGRAM_ITEM,
    },
  },
  // PROGRAM_ITEM -> TYPE_DECL
  [OAK_PARSER_RULE_PROGRAM_ITEM] = {
    .op = OAK_PARSER_OP_CHOICE,
    .rules = {
      OAK_PARSER_RULE_TYPE_DECL,
    }
  },
  // TYPE_DECL -> TYPE_KEYWORD TYPE_NAME LBRACE TYPE_FIELD_DECLS RBRACE
  [OAK_PARSER_RULE_TYPE_DECL] = {
    .op = OAK_PARSER_OP_SEQUENCE,
    .rules = {
      OAK_PARSER_RULE_TYPE_KEYWORD,
      OAK_PARSER_RULE_TYPE_NAME,
      OAK_PARSER_RULE_LBRACE,
      OAK_PARSER_RULE_TYPE_FIELD_DECLS,
      OAK_PARSER_RULE_RBRACE,
    }
  },
  // TYPE_FIELD_DECLS -> (TYPE_FIELD_DECL)*
  [OAK_PARSER_RULE_TYPE_FIELD_DECLS] = {
    .op = OAK_PARSER_OP_REPEAT,
    .rules = {
      OAK_PARSER_RULE_TYPE_FIELD_DECL,
    }
  },
  // TYPE_FIELD_DECL -> IDENT COLON IDENT SEMICOLON
  [OAK_PARSER_RULE_TYPE_FIELD_DECL] = {
    .op = OAK_PARSER_OP_SEQUENCE,
    .rules = {
      OAK_PARSER_RULE_IDENT,
      OAK_PARSER_RULE_COLON,
      OAK_PARSER_RULE_IDENT,
      OAK_PARSER_RULE_SEMICOLON,
    },
  },
  // TYPE_KEYWORD -> 'type'
  [OAK_PARSER_RULE_TYPE_KEYWORD] = {
    .op = OAK_PARSER_OP_TOKEN,
    .tok_type = OAK_TOK_TYPE,
  },
  // TYPE_NAME -> IDENT
  [OAK_PARSER_RULE_TYPE_NAME] = {
    .op = OAK_PARSER_OP_TOKEN,
    .tok_type = OAK_TOK_IDENT,
  },
  // LBRACE -> '{'
  [OAK_PARSER_RULE_LBRACE] = {
    .op = OAK_PARSER_OP_TOKEN,
    .tok_type = OAK_TOK_LBRACE,
  },
  // RBRACE -> '}'
  [OAK_PARSER_RULE_RBRACE] = {
    .op = OAK_PARSER_OP_TOKEN,
    .tok_type = OAK_TOK_RBRACE,
  },
  // IDENT -> OAK_TOK_IDENT
  [OAK_PARSER_RULE_IDENT] = {
    .op = OAK_PARSER_OP_TOKEN,
    .tok_type = OAK_TOK_IDENT,
  },
  // COLON -> ':'
  [OAK_PARSER_RULE_COLON] = {
    .op = OAK_PARSER_OP_TOKEN,
    .tok_type = OAK_TOK_COLON,
  },
  // SEMICOLON -> ';'
  [OAK_PARSER_RULE_SEMICOLON] = {
    .op = OAK_PARSER_OP_TOKEN,
    .tok_type = OAK_TOK_SEMICOLON,
  },
};

int oak_parser_rule_is_token(const oak_parser_rule_id_t rule_id)
{
  return oak_grammar[rule_id].op == OAK_PARSER_OP_TOKEN;
}

oak_ast_node_t* _oak_parse(oak_parser_t* p, oak_parser_rule_id_t rule_id);

static oak_ast_node_t* make_ast_node_token(oak_parser_t* p,
                                           const oak_parser_rule_id_t rule_id)
{
  oak_assert(p);
  oak_assert(p->curr);
  oak_assert(p->curr->next);

  const oak_tok_t* tok = oak_container_of(p->curr, oak_tok_t, link);
  const oak_parser_rule_t* curr_rule = &oak_grammar[rule_id];
  if (tok->type != curr_rule->tok_type)
    return NULL;
  oak_ast_node_t* node = oak_mem_acquire(OAK_SRC_LOC, sizeof(oak_ast_node_t));
  if (!node)
    return NULL;
  node->rule_id = rule_id;
  node->tok = tok;

  // Advance parser
  if (p->curr)
    p->curr = p->curr->next;

  return node;
}

static oak_ast_node_t*
make_ast_node_sequence(oak_parser_t* p, const oak_parser_rule_id_t rule_id)
{
  oak_list_entry_t* saved = p->curr;
  oak_ast_node_t* node = NULL;
  const oak_parser_rule_t* curr_rule = &oak_grammar[rule_id];

  for (size_t i = 0; i < OAK_ARRAY_SIZE(curr_rule->rules) &&
                     curr_rule->rules[i] != OAK_PARSER_RULE_NONE;
       ++i)
  {
    oak_ast_node_t* child_node = _oak_parse(p, curr_rule->rules[i]);
    if (child_node)
    {
      // Allocate node if it's NULL
      if (!node)
      {
        node = oak_mem_acquire(OAK_SRC_LOC, sizeof(oak_ast_node_t));
        node->rule_id = rule_id;
        oak_list_init(&node->children);
      }
      oak_list_add_tail(&node->children, &child_node->link);
    }
    else
    {
      p->curr = saved;
      break;
    }
  }

  return node;
}

static oak_ast_node_t* make_ast_node_choice(oak_parser_t* p,
                                            const oak_parser_rule_id_t rule_id)
{
  oak_list_entry_t* saved = p->curr;
  const oak_parser_rule_t* curr_rule = &oak_grammar[rule_id];

  for (size_t i = 0; i < OAK_ARRAY_SIZE(curr_rule->rules) &&
                     curr_rule->rules[i] != OAK_PARSER_RULE_NONE;
       ++i)
  {
    oak_ast_node_t* child_node = _oak_parse(p, curr_rule->rules[i]);
    if (child_node)
    {
      oak_ast_node_t* node =
          oak_mem_acquire(OAK_SRC_LOC, sizeof(oak_ast_node_t));
      node->rule_id = rule_id;
      oak_list_init(&node->children);
      oak_list_add_tail(&node->children, &child_node->link);
      oak_log(OAK_LOG_DBG, "Node: %d", rule_id);
      return node;
    }
  }

  p->curr = saved;
  return NULL;
}

static oak_ast_node_t* make_ast_node_repeat(oak_parser_t* p,
                                            const oak_parser_rule_id_t rule_id)
{
  oak_ast_node_t* node = oak_mem_acquire(OAK_SRC_LOC, sizeof(oak_ast_node_t));
  node->rule_id = rule_id;
  oak_list_init(&node->children);

  const oak_parser_rule_t* curr_rule = &oak_grammar[rule_id];
  for (;;)
  {
    oak_ast_node_t* child_node = make_ast_node_sequence(p, curr_rule->rules[0]);
    if (!child_node)
      break;
    oak_list_add_tail(&node->children, &child_node->link);
  }

  return node;
}

oak_ast_node_t* _oak_parse(oak_parser_t* p, const oak_parser_rule_id_t rule_id)
{
  if (rule_id == OAK_PARSER_RULE_NONE)
    return NULL;
  if (p->curr == p->head)
    return NULL;

  oak_ast_node_t* node = NULL;
  const oak_parser_rule_t* curr_rule = &oak_grammar[rule_id];
  switch (curr_rule->op)
  {
  case OAK_PARSER_OP_TOKEN:
    node = make_ast_node_token(p, rule_id);
    break;
  case OAK_PARSER_OP_SEQUENCE:
    node = make_ast_node_sequence(p, rule_id);
    break;
  case OAK_PARSER_OP_CHOICE:
    node = make_ast_node_choice(p, rule_id);
    break;
  case OAK_PARSER_OP_REPEAT:
    node = make_ast_node_repeat(p, rule_id);
    break;
  case OAK_PARSER_OP_REPEAT_ONE:
  case OAK_PARSER_OP_OPTIONAL:
    break;
  }

  return node;
}

oak_ast_node_t* oak_parse(const oak_lex_t* lex,
                          const oak_parser_rule_id_t rule_id)
{
  oak_parser_t parser = {
    .head = &lex->tokens,
    .curr = lex->tokens.next,
  };

  return _oak_parse(&parser, rule_id);
}

void oak_ast_node_cleanup(oak_ast_node_t* node)
{
  if (!node)
    return;

  const oak_parser_rule_t* rule = &oak_grammar[node->rule_id];
  if (rule->op != OAK_PARSER_OP_TOKEN)
  {
    oak_list_entry_t *curr, *n;
    oak_list_for_each_safe(curr, n, &node->children)
    {
      oak_ast_node_t* child = oak_container_of(curr, oak_ast_node_t, link);
      oak_ast_node_cleanup(child);
    }
  }

  oak_mem_release(OAK_SRC_LOC, node);
}
