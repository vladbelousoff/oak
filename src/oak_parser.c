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

typedef enum
{
  OAK_GRAMMAR_OP_TOKEN,      // Match one specific token (terminal)
  OAK_GRAMMAR_OP_SEQUENCE,   // Match all children in order (A B C)
  OAK_GRAMMAR_OP_CHOICE,     // Match first succeeding child (A | B | C)
  OAK_GRAMMAR_OP_REPEAT,     // Match child zero or more times (A*)
  OAK_GRAMMAR_OP_REPEAT_ONE, // Match child one or more times (A+)
  OAK_GRAMMAR_OP_OPTIONAL,   // Match child zero or one times (A?)
} oak_grammar_op_t;

typedef struct
{
  oak_grammar_op_t op;
  union
  {
    oak_node_kind_t rules[16];
    oak_tok_type_t tok_type;
  };
} oak_grammar_entry_t;

static oak_grammar_entry_t oak_grammar[] = {
  // PROGRAM -> (PROGRAM_ITEM)*
  [OAK_NODE_KIND_PROGRAM] = {
    .op = OAK_GRAMMAR_OP_REPEAT,
    .rules = {
      OAK_NODE_KIND_PROGRAM_ITEM,
    },
  },
  // PROGRAM_ITEM -> TYPE_DECL | STATEMENT
  [OAK_NODE_KIND_PROGRAM_ITEM] = {
    .op = OAK_GRAMMAR_OP_CHOICE,
    .rules = {
      OAK_NODE_KIND_TYPE_DECL,
      OAK_NODE_KIND_STATEMENT,
    }
  },
  // TYPE_DECL -> TYPE_KEYWORD TYPE_NAME LBRACE TYPE_FIELD_DECLS RBRACE
  [OAK_NODE_KIND_TYPE_DECL] = {
    .op = OAK_GRAMMAR_OP_SEQUENCE,
    .rules = {
      OAK_NODE_KIND_TYPE_KEYWORD,
      OAK_NODE_KIND_TYPE_NAME,
      OAK_NODE_KIND_LBRACE,
      OAK_NODE_KIND_TYPE_FIELD_DECLS,
      OAK_NODE_KIND_RBRACE,
    }
  },
  // TYPE_FIELD_DECLS -> (TYPE_FIELD_DECL)*
  [OAK_NODE_KIND_TYPE_FIELD_DECLS] = {
    .op = OAK_GRAMMAR_OP_REPEAT,
    .rules = {
      OAK_NODE_KIND_TYPE_FIELD_DECL,
    }
  },
  // TYPE_FIELD_DECL -> IDENT COLON IDENT SEMICOLON
  [OAK_NODE_KIND_TYPE_FIELD_DECL] = {
    .op = OAK_GRAMMAR_OP_SEQUENCE,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_NODE_KIND_COLON,
      OAK_NODE_KIND_IDENT,
      OAK_NODE_KIND_SEMICOLON,
    },
  },
  // TYPE_KEYWORD -> 'type'
  [OAK_NODE_KIND_TYPE_KEYWORD] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_TYPE,
  },
  // TYPE_NAME -> IDENT
  [OAK_NODE_KIND_TYPE_NAME] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_IDENT,
  },
  // LBRACE -> '{'
  [OAK_NODE_KIND_LBRACE] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_LBRACE,
  },
  // RBRACE -> '}'
  [OAK_NODE_KIND_RBRACE] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_RBRACE,
  },
  // IDENT -> OAK_TOK_IDENT
  [OAK_NODE_KIND_IDENT] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_IDENT,
  },
  // COLON -> ':'
  [OAK_NODE_KIND_COLON] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_COLON,
  },
  // SEMICOLON -> ';'
  [OAK_NODE_KIND_SEMICOLON] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_SEMICOLON,
  },
  // STATEMENT -> EXPR SEMICOLON
  [OAK_NODE_KIND_STATEMENT] = {
    .op = OAK_GRAMMAR_OP_SEQUENCE,
    .rules = {
      OAK_NODE_KIND_EXPR,
      OAK_NODE_KIND_SEMICOLON,
    },
  },
  // EXPR -> INT | FLOAT | STRING
  [OAK_NODE_KIND_EXPR] = {
    .op = OAK_GRAMMAR_OP_CHOICE,
    .rules = {
      OAK_NODE_KIND_INT,
      OAK_NODE_KIND_FLOAT,
      OAK_NODE_KIND_STRING,
    },
  },
  // INT -> OAK_TOK_INT_NUM
  [OAK_NODE_KIND_INT] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_INT_NUM,
  },
  // FLOAT -> OAK_TOK_FLOAT_NUM
  [OAK_NODE_KIND_FLOAT] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_FLOAT_NUM,
  },
  // STRING -> OAK_TOK_STRING
  [OAK_NODE_KIND_STRING] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .tok_type = OAK_TOK_STRING,
  },
};

int oak_node_kind_is_token(const oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_OP_TOKEN;
}

static oak_ast_node_t* parse_rule(oak_parser_t* p, oak_node_kind_t kind);

static oak_ast_node_t* make_ast_node_token(oak_parser_t* p,
                                           const oak_node_kind_t kind)
{
  oak_assert(p);
  oak_assert(p->curr);
  oak_assert(p->curr->next);

  const oak_tok_t* tok = oak_container_of(p->curr, oak_tok_t, link);
  const oak_grammar_entry_t* entry = &oak_grammar[kind];
  if (tok->type != entry->tok_type)
    return NULL;
  oak_ast_node_t* node = oak_mem_acquire(OAK_SRC_LOC, sizeof(oak_ast_node_t));
  if (!node)
    return NULL;
  node->kind = kind;
  node->tok = tok;

  // Advance parser
  if (p->curr)
    p->curr = p->curr->next;

  return node;
}

static oak_ast_node_t*
make_ast_node_sequence(oak_parser_t* p, const oak_node_kind_t kind)
{
  oak_list_entry_t* saved = p->curr;
  oak_ast_node_t* node = NULL;
  const oak_grammar_entry_t* entry = &oak_grammar[kind];

  for (size_t i = 0; i < OAK_ARRAY_SIZE(entry->rules) &&
                     entry->rules[i] != OAK_NODE_KIND_NONE;
       ++i)
  {
    oak_ast_node_t* child = parse_rule(p, entry->rules[i]);
    if (child)
    {
      // Allocate node if it's NULL
      if (!node)
      {
        node = oak_mem_acquire(OAK_SRC_LOC, sizeof(oak_ast_node_t));
        node->kind = kind;
        oak_list_init(&node->children);
      }
      oak_list_add_tail(&node->children, &child->link);
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
                                            const oak_node_kind_t kind)
{
  oak_list_entry_t* saved = p->curr;
  const oak_grammar_entry_t* entry = &oak_grammar[kind];

  for (size_t i = 0; i < OAK_ARRAY_SIZE(entry->rules) &&
                     entry->rules[i] != OAK_NODE_KIND_NONE;
       ++i)
  {
    oak_ast_node_t* child = parse_rule(p, entry->rules[i]);
    if (child)
    {
      oak_ast_node_t* node =
          oak_mem_acquire(OAK_SRC_LOC, sizeof(oak_ast_node_t));
      node->kind = kind;
      oak_list_init(&node->children);
      oak_list_add_tail(&node->children, &child->link);
      return node;
    }
  }

  p->curr = saved;
  return NULL;
}

static oak_ast_node_t* make_ast_node_repeat(oak_parser_t* p,
                                            const oak_node_kind_t kind)
{
  oak_ast_node_t* node = oak_mem_acquire(OAK_SRC_LOC, sizeof(oak_ast_node_t));
  node->kind = kind;
  oak_list_init(&node->children);

  const oak_grammar_entry_t* entry = &oak_grammar[kind];
  for (;;)
  {
    oak_ast_node_t* child = parse_rule(p, entry->rules[0]);
    if (!child)
      break;
    oak_list_add_tail(&node->children, &child->link);
  }

  return node;
}

static oak_ast_node_t* parse_rule(oak_parser_t* p, const oak_node_kind_t kind)
{
  if (kind == OAK_NODE_KIND_NONE)
    return NULL;
  if (p->curr == p->head)
    return NULL;

  oak_ast_node_t* node = NULL;
  const oak_grammar_entry_t* entry = &oak_grammar[kind];
  switch (entry->op)
  {
  case OAK_GRAMMAR_OP_TOKEN:
    node = make_ast_node_token(p, kind);
    break;
  case OAK_GRAMMAR_OP_SEQUENCE:
    node = make_ast_node_sequence(p, kind);
    break;
  case OAK_GRAMMAR_OP_CHOICE:
    node = make_ast_node_choice(p, kind);
    break;
  case OAK_GRAMMAR_OP_REPEAT:
    node = make_ast_node_repeat(p, kind);
    break;
  case OAK_GRAMMAR_OP_REPEAT_ONE:
  case OAK_GRAMMAR_OP_OPTIONAL:
    break;
  }

  return node;
}

oak_ast_node_t* oak_parse(const oak_lex_t* lex, const oak_node_kind_t kind)
{
  oak_parser_t parser = {
    .head = &lex->tokens,
    .curr = lex->tokens.next,
  };

  return parse_rule(&parser, kind);
}

void oak_ast_node_cleanup(oak_ast_node_t* node)
{
  if (!node)
    return;

  const oak_grammar_entry_t* entry = &oak_grammar[node->kind];
  if (entry->op != OAK_GRAMMAR_OP_TOKEN)
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
