#include "oak_parser.h"
#include <stdio.h>

#include "oak_arena.h"
#include "oak_common.h"
#include "oak_log.h"
#include "oak_mem.h"

struct oak_parser_result_t
{
  oak_ast_node_t* root;
  oak_arena_t arena;
};

typedef struct
{
  const oak_list_head_t* head;
  oak_list_head_t* curr;
  oak_arena_t* arena;
} oak_parser_t;

#define OAK_NODE_SKIP      ((oak_node_kind_t)(1 << 15))
#define OAK_NODE_KIND_MASK ((oak_node_kind_t)~OAK_NODE_SKIP)

typedef enum
{
  OAK_GRAMMAR_OP_TOKEN,      // Match one specific token (terminal)
  OAK_GRAMMAR_OP_SEQUENCE,   // Match all children in order (A B C)
  OAK_GRAMMAR_OP_CHOICE,     // Match first succeeding child (A | B | C)
  OAK_GRAMMAR_OP_REPEAT,     // Match child zero or more times (A*)
  OAK_GRAMMAR_OP_REPEAT_ONE, // Match child one or more times (A+)
  OAK_GRAMMAR_OP_OPTIONAL,   // Match child zero or one times (A?)
  OAK_GRAMMAR_OP_PRATT,      // Pratt parser for operator precedence
  OAK_GRAMMAR_OP_UNARY,      // Unary node with a single child
  OAK_GRAMMAR_OP_BINARY,     // Binary node with lhs and rhs
} oak_grammar_op_t;

typedef struct
{
  oak_token_kind_t op_token;
  int lbp;
  int rbp;
  oak_node_kind_t node_kind;
} oak_pratt_rule_t;

typedef struct
{
  oak_grammar_op_t op;
  union
  {
    oak_node_kind_t rules[16];
    oak_token_kind_t token_kind;
    struct
    {
      oak_node_kind_t primary_rule;
      const oak_pratt_rule_t* prefix;
      const oak_pratt_rule_t* infix;
    } pratt;
  };
} oak_grammar_entry_t;

static const oak_pratt_rule_t expr_prefix[] = {
  { OAK_TOKEN_MINUS, 0, 13, OAK_NODE_KIND_UNARY_NEG },
  { OAK_TOKEN_EXCLAMATION_MARK, 0, 13, OAK_NODE_KIND_UNARY_NOT },
  { 0 },
};

static const oak_pratt_rule_t expr_infix[] = {
  { OAK_TOKEN_OR, 1, 2, OAK_NODE_KIND_BINARY_OR },
  { OAK_TOKEN_AND, 3, 4, OAK_NODE_KIND_BINARY_AND },
  { OAK_TOKEN_EQUAL, 5, 6, OAK_NODE_KIND_BINARY_EQ },
  { OAK_TOKEN_NOT_EQUAL, 5, 6, OAK_NODE_KIND_BINARY_NEQ },
  { OAK_TOKEN_LESS, 7, 8, OAK_NODE_KIND_BINARY_LESS },
  { OAK_TOKEN_LESS_EQUAL, 7, 8, OAK_NODE_KIND_BINARY_LESS_EQ },
  { OAK_TOKEN_GREATER, 7, 8, OAK_NODE_KIND_BINARY_GREATER },
  { OAK_TOKEN_GREATER_EQUAL, 7, 8, OAK_NODE_KIND_BINARY_GREATER_EQ },
  { OAK_TOKEN_PLUS, 9, 10, OAK_NODE_KIND_BINARY_ADD },
  { OAK_TOKEN_MINUS, 9, 10, OAK_NODE_KIND_BINARY_SUB },
  { OAK_TOKEN_STAR, 11, 12, OAK_NODE_KIND_BINARY_MUL },
  { OAK_TOKEN_SLASH, 11, 12, OAK_NODE_KIND_BINARY_DIV },
  { OAK_TOKEN_PERCENT, 11, 12, OAK_NODE_KIND_BINARY_MOD },
  { 0 },
};

static oak_grammar_entry_t oak_grammar[] = {
  // PROGRAM -> (PROGRAM_ITEM)*
  [OAK_NODE_KIND_PROGRAM] = {
    .op = OAK_GRAMMAR_OP_REPEAT,
    .rules = {
      OAK_NODE_KIND_PROGRAM_ITEM,
    },
  },
  // PROGRAM_ITEM -> STRUCT_DECL | STMT
  [OAK_NODE_KIND_PROGRAM_ITEM] = {
    .op = OAK_GRAMMAR_OP_CHOICE,
    .rules = {
      OAK_NODE_KIND_STRUCT_DECL,
      OAK_NODE_KIND_STMT,
    }
  },
  // STRUCT_DECL -> TYPE_KEYWORD TYPE_NAME STRUCT_KEYWORD LBRACE STRUCT_FIELD_DECLS RBRACE
  [OAK_NODE_KIND_STRUCT_DECL] = {
    .op = OAK_GRAMMAR_OP_SEQUENCE,
    .rules = {
      OAK_NODE_KIND_TYPE_KEYWORD | OAK_NODE_SKIP,
      OAK_NODE_KIND_TYPE_NAME,
      OAK_NODE_KIND_STRUCT_KEYWORD | OAK_NODE_SKIP,
      OAK_NODE_KIND_LBRACE | OAK_NODE_SKIP,
      OAK_NODE_KIND_STRUCT_FIELD_DECLS,
      OAK_NODE_KIND_RBRACE | OAK_NODE_SKIP,
    }
  },
  // STRUCT_FIELD_DECLS -> (STRUCT_FIELD_DECL)*
  [OAK_NODE_KIND_STRUCT_FIELD_DECLS] = {
    .op = OAK_GRAMMAR_OP_REPEAT,
    .rules = {
      OAK_NODE_KIND_STRUCT_FIELD_DECL,
    }
  },
  // STRUCT_FIELD_DECL -> IDENT COLON IDENT SEMICOLON
  [OAK_NODE_KIND_STRUCT_FIELD_DECL] = {
    .op = OAK_GRAMMAR_OP_BINARY,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_NODE_KIND_COLON | OAK_NODE_SKIP,
      OAK_NODE_KIND_IDENT,
      OAK_NODE_KIND_SEMICOLON | OAK_NODE_SKIP,
    },
  },
  // TYPE_KEYWORD -> 'type'
  [OAK_NODE_KIND_TYPE_KEYWORD] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_TYPE,
  },
  // STRUCT_KEYWORD -> 'struct'
  [OAK_NODE_KIND_STRUCT_KEYWORD] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_STRUCT,
  },
  // TYPE_NAME -> IDENT
  [OAK_NODE_KIND_TYPE_NAME] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_IDENT,
  },
  // LBRACE -> '{'
  [OAK_NODE_KIND_LBRACE] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_LBRACE,
  },
  // RBRACE -> '}'
  [OAK_NODE_KIND_RBRACE] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_RBRACE,
  },
  // IDENT -> OAK_TOKEN_IDENT
  [OAK_NODE_KIND_IDENT] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_IDENT,
  },
  // COLON -> ':'
  [OAK_NODE_KIND_COLON] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_COLON,
  },
  // SEMICOLON -> ';'
  [OAK_NODE_KIND_SEMICOLON] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_SEMICOLON,
  },
  // STMT -> STMT_LET_ASSIGNMENT | STMT_EXPR | STMT_ASSIGNMENT
  [OAK_NODE_KIND_STMT] = {
    .op = OAK_GRAMMAR_OP_CHOICE,
    .rules = {
      OAK_NODE_KIND_STMT_LET_ASSIGNMENT,
      OAK_NODE_KIND_STMT_EXPR,
      OAK_NODE_KIND_STMT_ASSIGNMENT,
    }
  },
  // STMT_EXPR -> EXPR SEMICOLON
  [OAK_NODE_KIND_STMT_EXPR] = {
    .op = OAK_GRAMMAR_OP_SEQUENCE,
    .rules = {
      OAK_NODE_KIND_EXPR,
      OAK_NODE_KIND_SEMICOLON | OAK_NODE_SKIP,
    },
  },
  // EXPR -> Pratt-parsed expression with operator precedence
  [OAK_NODE_KIND_EXPR] = {
    .op = OAK_GRAMMAR_OP_PRATT,
    .pratt = {
      .primary_rule = OAK_NODE_KIND_EXPR_PRIMARY,
      .prefix = expr_prefix,
      .infix = expr_infix,
    },
  },
  // EXPR_PRIMARY -> INT | FLOAT | STRING | IDENT
  [OAK_NODE_KIND_EXPR_PRIMARY] = {
    .op = OAK_GRAMMAR_OP_CHOICE,
    .rules = {
      OAK_NODE_KIND_INT,
      OAK_NODE_KIND_FLOAT,
      OAK_NODE_KIND_STRING,
      OAK_NODE_KIND_IDENT,
    },
  },
  // INT -> OAK_TOKEN_INT_NUM
  [OAK_NODE_KIND_INT] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_INT_NUM,
  },
  // FLOAT -> OAK_TOKEN_FLOAT_NUM
  [OAK_NODE_KIND_FLOAT] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_FLOAT_NUM,
  },
  // STRING -> OAK_TOKEN_STRING
  [OAK_NODE_KIND_STRING] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_STRING,
  },
  // STMT_ASSIGNMENT -> IDENT ASSIGN EXPR SEMICOLON
  [OAK_NODE_KIND_STMT_ASSIGNMENT] = {
    .op = OAK_GRAMMAR_OP_SEQUENCE,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_NODE_KIND_ASSIGN | OAK_NODE_SKIP,
      OAK_NODE_KIND_EXPR,
      OAK_NODE_KIND_SEMICOLON | OAK_NODE_SKIP,
    },
  },
  // ASSIGN -> OAK_TOKEN_ASSIGN
  [OAK_NODE_KIND_ASSIGN] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_ASSIGN,
  },
  // STMT_LET_ASSIGNMENT -> LET STMT_ASSIGNMENT
  [OAK_NODE_KIND_STMT_LET_ASSIGNMENT] = {
    .op = OAK_GRAMMAR_OP_SEQUENCE,
    .rules = {
      OAK_NODE_KIND_LET_KEYWORD | OAK_NODE_SKIP,
      OAK_NODE_KIND_STMT_ASSIGNMENT,
    },
  },
  // LET -> OAK_TOKEN_LET
  [OAK_NODE_KIND_LET_KEYWORD] = {
    .op = OAK_GRAMMAR_OP_TOKEN,
    .token_kind = OAK_TOKEN_LET,
  },
  [OAK_NODE_KIND_BINARY_ADD]        = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_SUB]        = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_MUL]        = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_DIV]        = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_MOD]        = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_EQ]         = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_NEQ]        = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_LESS]       = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_LESS_EQ]    = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_GREATER]    = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_GREATER_EQ] = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_AND]        = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_BINARY_OR]         = { .op = OAK_GRAMMAR_OP_BINARY },
  [OAK_NODE_KIND_UNARY_NEG]         = { .op = OAK_GRAMMAR_OP_UNARY },
  [OAK_NODE_KIND_UNARY_NOT]         = { .op = OAK_GRAMMAR_OP_UNARY },
};

int oak_node_grammar_op_token(const oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_OP_TOKEN;
}

int oak_node_grammar_op_unary(const oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_OP_UNARY;
}

int oak_node_grammar_op_binary(const oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_OP_BINARY;
}

static oak_ast_node_t* parse_rule(oak_parser_t* p, oak_node_kind_t kind);

static int try_skip_token(oak_parser_t* p, const oak_node_kind_t child_kind)
{
  const oak_token_t* token = oak_container_of(p->curr, oak_token_t, link);
  if (token->kind != oak_grammar[child_kind].token_kind)
    return 0;
  p->curr = p->curr->next;
  return 1;
}

static size_t grammar_rule_count(const oak_grammar_entry_t* entry)
{
  size_t n = 0;
  while (n < OAK_ARRAY_SIZE(entry->rules) &&
         (entry->rules[n] & OAK_NODE_KIND_MASK) != OAK_NODE_KIND_NONE)
    ++n;
  return n;
}

static const oak_pratt_rule_t*
find_pratt_rule(const oak_pratt_rule_t* rules,
                const oak_token_kind_t token_kind)
{
  for (const oak_pratt_rule_t* r = rules; r->node_kind != OAK_NODE_KIND_NONE;
       r++)
  {
    if (token_kind == r->op_token)
      return r;
  }
  return NULL;
}

static oak_ast_node_t* make_ast_node_token(oak_parser_t* p,
                                           const oak_node_kind_t kind)
{
  oak_assert(p);
  oak_assert(p->curr);
  oak_assert(p->curr->next);
  const oak_token_t* token = oak_container_of(p->curr, oak_token_t, link);
  const oak_grammar_entry_t* entry = &oak_grammar[kind];
  if (token->kind != entry->token_kind)
    return NULL;
  oak_ast_node_t* node = oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
  if (!node)
    return NULL;
  node->kind = kind;
  node->token = token;
  p->curr = p->curr->next;
  return node;
}

static oak_ast_node_t* make_ast_node_sequence(oak_parser_t* p,
                                              const oak_node_kind_t kind)
{
  oak_list_entry_t* saved = p->curr;
  oak_ast_node_t* node = NULL;
  const oak_grammar_entry_t* entry = &oak_grammar[kind];

  const size_t seq_count = grammar_rule_count(entry);
  for (size_t i = 0; i < seq_count; ++i)
  {
    const oak_node_kind_t rule = entry->rules[i];
    const oak_node_kind_t child_kind = rule & OAK_NODE_KIND_MASK;
    if (rule & OAK_NODE_SKIP)
    {
      if (!try_skip_token(p, child_kind))
      {
        p->curr = saved;
        node = NULL;
        break;
      }
      continue;
    }
    oak_ast_node_t* child = parse_rule(p, child_kind);
    if (child)
    {
      if (!node)
      {
        node = oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
        node->kind = kind;
        oak_list_init(&node->children);
      }
      oak_list_add_tail(&node->children, &child->link);
    }
    else
    {
      p->curr = saved;
      node = NULL;
      break;
    }
  }

  return node;
}

static oak_ast_node_t* make_ast_node_binary(oak_parser_t* p,
                                            const oak_node_kind_t kind)
{
  oak_list_entry_t* saved = p->curr;
  const oak_grammar_entry_t* entry = &oak_grammar[kind];
  oak_ast_node_t* lhs = NULL;
  oak_ast_node_t* rhs = NULL;

  const size_t bin_count = grammar_rule_count(entry);
  for (size_t i = 0; i < bin_count; ++i)
  {
    const oak_node_kind_t rule = entry->rules[i];
    const oak_node_kind_t child_kind = rule & OAK_NODE_KIND_MASK;
    if (rule & OAK_NODE_SKIP)
    {
      if (!try_skip_token(p, child_kind))
      {
        p->curr = saved;
        return NULL;
      }
      continue;
    }
    oak_ast_node_t* child = parse_rule(p, child_kind);
    if (!child)
    {
      p->curr = saved;
      return NULL;
    }
    if (!lhs)
      lhs = child;
    else
      rhs = child;
  }

  if (!lhs || !rhs)
  {
    p->curr = saved;
    return NULL;
  }

  oak_ast_node_t* node = oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
  if (!node)
  {
    p->curr = saved;
    return NULL;
  }
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static oak_ast_node_t* make_ast_node_choice(oak_parser_t* p,
                                            const oak_node_kind_t kind)
{
  oak_list_entry_t* saved = p->curr;
  const oak_grammar_entry_t* entry = &oak_grammar[kind];

  const size_t choice_count = grammar_rule_count(entry);
  for (size_t i = 0; i < choice_count; ++i)
  {
    oak_ast_node_t* child = parse_rule(p, entry->rules[i]);
    if (child)
      return child;
  }

  p->curr = saved;
  return NULL;
}

static oak_ast_node_t* make_ast_node_repeat(oak_parser_t* p,
                                            const oak_node_kind_t kind)
{
  oak_ast_node_t* node = oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
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

static oak_ast_node_t*
parse_pratt(oak_parser_t* p, const oak_node_kind_t kind, const int min_bp)
{
  const oak_grammar_entry_t* entry = &oak_grammar[kind];
  oak_ast_node_t* lhs = NULL;

  if (p->curr != p->head && entry->pratt.prefix)
  {
    const oak_token_t* token = oak_container_of(p->curr, oak_token_t, link);
    const oak_pratt_rule_t* r =
        find_pratt_rule(entry->pratt.prefix, token->kind);
    if (r)
    {
      p->curr = p->curr->next;
      oak_ast_node_t* operand = parse_pratt(p, kind, r->rbp);
      if (!operand)
        return NULL;

      lhs = oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
      if (!lhs)
        return NULL;
      lhs->kind = r->node_kind;
      lhs->child = operand;
    }
  }

  if (!lhs && p->curr != p->head)
  {
    const oak_token_t* peek = oak_container_of(p->curr, oak_token_t, link);
    if (peek->kind == OAK_TOKEN_LPAREN)
    {
      p->curr = p->curr->next;
      lhs = parse_pratt(p, kind, 0);
      if (!lhs || p->curr == p->head)
        return NULL;
      peek = oak_container_of(p->curr, oak_token_t, link);
      if (peek->kind != OAK_TOKEN_RPAREN)
        return NULL;
      p->curr = p->curr->next;
    }
  }

  if (!lhs)
  {
    lhs = parse_rule(p, entry->pratt.primary_rule);
    if (!lhs)
      return NULL;
  }

  for (;;)
  {
    if (p->curr == p->head)
      break;
    const oak_token_t* token = oak_container_of(p->curr, oak_token_t, link);

    const oak_pratt_rule_t* rule =
        find_pratt_rule(entry->pratt.infix, token->kind);
    if (!rule || rule->lbp < min_bp)
      break;

    p->curr = p->curr->next;
    oak_ast_node_t* rhs = parse_pratt(p, kind, rule->rbp);
    if (!rhs)
      return NULL;

    oak_ast_node_t* node = oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
    if (!node)
      return NULL;
    node->kind = rule->node_kind;
    node->lhs = lhs;
    node->rhs = rhs;
    lhs = node;
  }

  return lhs;
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
  case OAK_GRAMMAR_OP_BINARY:
    node = make_ast_node_binary(p, kind);
    break;
  case OAK_GRAMMAR_OP_REPEAT_ONE:
  case OAK_GRAMMAR_OP_OPTIONAL:
  case OAK_GRAMMAR_OP_UNARY:
    break;
  case OAK_GRAMMAR_OP_PRATT:
    node = parse_pratt(p, kind, 0);
    break;
  }

  return node;
}

oak_parser_result_t* oak_parse(const oak_lexer_result_t* lexer,
                               const oak_node_kind_t kind)
{
  oak_parser_result_t* result =
      oak_alloc(sizeof(oak_parser_result_t), OAK_SRC_LOC);
  if (!result)
    return NULL;

  oak_arena_init(&result->arena, 0);

  const oak_list_head_t* tokens = oak_lexer_tokens(lexer);
  oak_parser_t parser = {
    .head = tokens,
    .curr = tokens->next,
    .arena = &result->arena,
  };

  result->root = parse_rule(&parser, kind);

  if (parser.curr != parser.head)
  {
    const oak_token_t* token = oak_container_of(parser.curr, oak_token_t, link);
    oak_log(OAK_LOG_ERR,
            "parse error at %d:%d: unexpected token '%s'",
            token->line,
            token->column,
            token->kind != OAK_TOKEN_IDENT ? oak_token_name(token->kind)
                                           : token->buf);
    oak_parser_cleanup(result);
    return NULL;
  }

  return result;
}

oak_ast_node_t* oak_parser_root(const oak_parser_result_t* result)
{
  return result ? result->root : NULL;
}

void oak_parser_cleanup(oak_parser_result_t* result)
{
  if (!result)
    return;
  oak_arena_destroy(&result->arena);
  oak_free(result, OAK_SRC_LOC);
}
