#include "oak_parser.h"

#include "oak_arena.h"
#include "oak_common.h"
#include "oak_log.h"
#include "oak_mem.h"

// ReSharper disable once CppClassNeverUsed
struct _oak_parser_result_t
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

typedef unsigned short oak_rule_item_t;

#define OAK_RULE_TOKEN    ((oak_rule_item_t)(1 << 15))
#define OAK_RULE_REPEAT   ((oak_rule_item_t)(1 << 14))
#define OAK_RULE_OPTIONAL ((oak_rule_item_t)(1 << 13))
#define OAK_RULE_KIND_MASK                                                     \
  ((oak_rule_item_t) ~(OAK_RULE_TOKEN | OAK_RULE_REPEAT | OAK_RULE_OPTIONAL))

typedef enum
{
  OAK_GRAMMAR_SEQUENCE, // Match all children in order (default)
  OAK_GRAMMAR_TOKEN,    // Match one specific token (terminal)
  OAK_GRAMMAR_CHOICE,   // Match first succeeding child (A | B | C)
  OAK_GRAMMAR_PRATT,    // Pratt parser for operator precedence
  OAK_GRAMMAR_BINARY,   // Produce binary node (lhs/rhs)
  OAK_GRAMMAR_UNARY,    // Produce unary node (single child)
} oak_grammar_op_t;

typedef enum
{
  OAK_PRATT_END,
  OAK_PRATT_OP,
  OAK_PRATT_GROUP,
  OAK_PRATT_CALL,
} oak_pratt_op_t;

typedef struct
{
  oak_pratt_op_t kind;
  oak_token_kind_t trigger_token;
  int l_bp;
  int r_bp;
  oak_node_kind_t node_kind;
  oak_token_kind_t close_token;
  oak_node_kind_t arg_rule;
} oak_pratt_rule_t;

typedef struct
{
  oak_grammar_op_t op;
  union
  {
    oak_rule_item_t rules[16];
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
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_MINUS,
      .r_bp = 13,
      .node_kind = OAK_NODE_KIND_UNARY_NEG,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_EXCLAMATION_MARK,
      .r_bp = 13,
      .node_kind = OAK_NODE_KIND_UNARY_NOT,
  },
  {
      .kind = OAK_PRATT_GROUP,
      .trigger_token = OAK_TOKEN_LPAREN,
      .close_token = OAK_TOKEN_RPAREN,
  },
  { 0 },
};

static const oak_pratt_rule_t expr_infix[] = {
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_OR,
      .l_bp = 1,
      .r_bp = 2,
      .node_kind = OAK_NODE_KIND_BINARY_OR,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_AND,
      .l_bp = 3,
      .r_bp = 4,
      .node_kind = OAK_NODE_KIND_BINARY_AND,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_EQUAL,
      .l_bp = 5,
      .r_bp = 6,
      .node_kind = OAK_NODE_KIND_BINARY_EQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_NOT_EQUAL,
      .l_bp = 5,
      .r_bp = 6,
      .node_kind = OAK_NODE_KIND_BINARY_NEQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_LESS,
      .l_bp = 7,
      .r_bp = 8,
      .node_kind = OAK_NODE_KIND_BINARY_LESS,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_LESS_EQUAL,
      .l_bp = 7,
      .r_bp = 8,
      .node_kind = OAK_NODE_KIND_BINARY_LESS_EQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_GREATER,
      .l_bp = 7,
      .r_bp = 8,
      .node_kind = OAK_NODE_KIND_BINARY_GREATER,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_GREATER_EQUAL,
      .l_bp = 7,
      .r_bp = 8,
      .node_kind = OAK_NODE_KIND_BINARY_GREATER_EQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_PLUS,
      .l_bp = 9,
      .r_bp = 10,
      .node_kind = OAK_NODE_KIND_BINARY_ADD,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_MINUS,
      .l_bp = 9,
      .r_bp = 10,
      .node_kind = OAK_NODE_KIND_BINARY_SUB,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_STAR,
      .l_bp = 11,
      .r_bp = 12,
      .node_kind = OAK_NODE_KIND_BINARY_MUL,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_SLASH,
      .l_bp = 11,
      .r_bp = 12,
      .node_kind = OAK_NODE_KIND_BINARY_DIV,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_PERCENT,
      .l_bp = 11,
      .r_bp = 12,
      .node_kind = OAK_NODE_KIND_BINARY_MOD,
  },
  {
      .kind = OAK_PRATT_CALL,
      .trigger_token = OAK_TOKEN_LPAREN,
      .l_bp = 15,
      .node_kind = OAK_NODE_KIND_FN_CALL,
      .close_token = OAK_TOKEN_RPAREN,
      .arg_rule = OAK_NODE_KIND_FN_CALL_ARG,
  },
  { 0 },
};

static oak_grammar_entry_t oak_grammar[] = {
  // PROGRAM -> PROGRAM_ITEM*
  [OAK_NODE_KIND_PROGRAM] = {
    .rules = {
      OAK_NODE_KIND_PROGRAM_ITEM | OAK_RULE_REPEAT,
    },
  },
  // PROGRAM_ITEM -> FN_DECL | STRUCT_DECL | ENUM_DECL | STMT
  [OAK_NODE_KIND_PROGRAM_ITEM] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_KIND_FN_DECL,
      OAK_NODE_KIND_STRUCT_DECL,
      OAK_NODE_KIND_ENUM_DECL,
      OAK_NODE_KIND_STMT,
    },
  },
  // STRUCT_DECL -> 'type' TYPE_NAME 'struct' '{' STRUCT_FIELD_DECL* '}'
  [OAK_NODE_KIND_STRUCT_DECL] = {
    .rules = {
      OAK_TOKEN_TYPE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_TYPE_NAME,
      OAK_TOKEN_STRUCT | OAK_RULE_TOKEN,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_STRUCT_FIELD_DECL | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // STRUCT_FIELD_DECL -> IDENT ':' IDENT ';'
  [OAK_NODE_KIND_STRUCT_FIELD_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // ENUM_DECL -> 'type' IDENT 'enum' '{' IDENT* '}'
  [OAK_NODE_KIND_ENUM_DECL] = {
    .rules = {
      OAK_TOKEN_TYPE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_ENUM | OAK_RULE_TOKEN,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_IDENT | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  [OAK_NODE_KIND_TYPE_NAME] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_IDENT,
  },
  [OAK_NODE_KIND_IDENT] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_IDENT,
  },
  // STMT -> STMT_IF | STMT_WHILE | STMT_FOR | STMT_BREAK | STMT_CONTINUE
  //       | STMT_RETURN | STMT_LET_ASSIGNMENT | STMT_ASSIGNMENT | STMT_EXPR
  [OAK_NODE_KIND_STMT] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_KIND_STMT_IF,
      OAK_NODE_KIND_STMT_WHILE,
      OAK_NODE_KIND_STMT_FOR,
      OAK_NODE_KIND_STMT_BREAK,
      OAK_NODE_KIND_STMT_CONTINUE,
      OAK_NODE_KIND_STMT_RETURN,
      OAK_NODE_KIND_STMT_LET_ASSIGNMENT,
      OAK_NODE_KIND_STMT_ASSIGNMENT,
      OAK_NODE_KIND_STMT_ADD_ASSIGN,
      OAK_NODE_KIND_STMT_SUB_ASSIGN,
      OAK_NODE_KIND_STMT_MUL_ASSIGN,
      OAK_NODE_KIND_STMT_DIV_ASSIGN,
      OAK_NODE_KIND_STMT_MOD_ASSIGN,
      OAK_NODE_KIND_STMT_EXPR,
    },
  },
  // STMT_EXPR -> EXPR ';'
  [OAK_NODE_KIND_STMT_EXPR] = {
    .rules = {
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  [OAK_NODE_KIND_EXPR] = {
    .op = OAK_GRAMMAR_PRATT,
    .pratt = {
      .primary_rule = OAK_NODE_KIND_EXPR_PRIMARY,
      .prefix = expr_prefix,
      .infix = expr_infix,
    },
  },
  // EXPR_PRIMARY -> INT | FLOAT | STRING | IDENT
  [OAK_NODE_KIND_EXPR_PRIMARY] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_KIND_INT,
      OAK_NODE_KIND_FLOAT,
      OAK_NODE_KIND_STRING,
      OAK_NODE_KIND_IDENT,
    },
  },
  [OAK_NODE_KIND_INT] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_INT_NUM,
  },
  [OAK_NODE_KIND_FLOAT] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_FLOAT_NUM,
  },
  [OAK_NODE_KIND_STRING] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_STRING,
  },
  // STMT_ASSIGNMENT -> IDENT '=' EXPR ';'
  [OAK_NODE_KIND_STMT_ASSIGNMENT] = {
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_LET_ASSIGNMENT -> 'let' STMT_ASSIGNMENT
  [OAK_NODE_KIND_STMT_LET_ASSIGNMENT] = {
    .rules = {
      OAK_TOKEN_LET | OAK_RULE_TOKEN,
      OAK_NODE_KIND_STMT_ASSIGNMENT,
    },
  },
  // FN_DECL -> 'fn' FN_RECEIVER? IDENT '(' FN_PARAM* ')' ('->' TYPE_NAME)? '{' STMT* '}'
  [OAK_NODE_KIND_FN_DECL] = {
    .rules = {
      OAK_TOKEN_FN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_FN_RECEIVER | OAK_RULE_OPTIONAL,
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_LPAREN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_FN_PARAM | OAK_RULE_REPEAT,
      OAK_TOKEN_RPAREN | OAK_RULE_TOKEN,
      OAK_TOKEN_ARROW | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
      OAK_NODE_KIND_TYPE_NAME | OAK_RULE_OPTIONAL,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_STMT | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // FN_RECEIVER -> IDENT '.'
  [OAK_NODE_KIND_FN_RECEIVER] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_DOT | OAK_RULE_TOKEN,
    },
  },
  // FN_PARAM -> MUT_KEYWORD? IDENT ':' IDENT ','?
  [OAK_NODE_KIND_FN_PARAM] = {
    .rules = {
      OAK_NODE_KIND_MUT_KEYWORD | OAK_RULE_OPTIONAL,
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
    },
  },
  [OAK_NODE_KIND_MUT_KEYWORD] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_MUT,
  },
  [OAK_NODE_KIND_BINARY_ADD]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_SUB]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_MUL]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_DIV]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_MOD]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_EQ]         = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_NEQ]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_LESS]       = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_LESS_EQ]    = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_GREATER]    = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_GREATER_EQ] = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_AND]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_BINARY_OR]         = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_KIND_UNARY_NEG]         = { .op = OAK_GRAMMAR_UNARY },
  [OAK_NODE_KIND_UNARY_NOT]         = { .op = OAK_GRAMMAR_UNARY },
  // FN_CALL_ARG -> IDENT '=' EXPR ','?
  [OAK_NODE_KIND_FN_CALL_ARG] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
    },
  },
  // STMT_RETURN -> 'return' EXPR ';'
  [OAK_NODE_KIND_STMT_RETURN] = {
    .rules = {
      OAK_TOKEN_RETURN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_IF -> 'if' EXPR '{' STMT* '}' ELSE_BLOCK?
  [OAK_NODE_KIND_STMT_IF] = {
    .rules = {
      OAK_TOKEN_IF | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_STMT | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_ELSE_BLOCK | OAK_RULE_OPTIONAL,
    },
  },
  // STMT_WHILE -> 'while' EXPR '{' STMT* '}'
  [OAK_NODE_KIND_STMT_WHILE] = {
    .rules = {
      OAK_TOKEN_WHILE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_STMT | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // STMT_FOR -> 'for' IDENT 'from' EXPR 'to' EXPR '{' STMT* '}'
  [OAK_NODE_KIND_STMT_FOR] = {
    .rules = {
      OAK_TOKEN_FOR | OAK_RULE_TOKEN,
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_FROM | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_TO | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_STMT | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // STMT_BREAK -> 'break' ';'
  [OAK_NODE_KIND_STMT_BREAK] = {
    .rules = {
      OAK_TOKEN_BREAK | OAK_RULE_TOKEN,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_CONTINUE -> 'continue' ';'
  [OAK_NODE_KIND_STMT_CONTINUE] = {
    .rules = {
      OAK_TOKEN_CONTINUE | OAK_RULE_TOKEN,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_ADD_ASSIGN -> IDENT '+=' EXPR ';'
  [OAK_NODE_KIND_STMT_ADD_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_PLUS_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_SUB_ASSIGN -> IDENT '-=' EXPR ';'
  [OAK_NODE_KIND_STMT_SUB_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_MINUS_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_MUL_ASSIGN -> IDENT '*=' EXPR ';'
  [OAK_NODE_KIND_STMT_MUL_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_STAR_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_DIV_ASSIGN -> IDENT '/=' EXPR ';'
  [OAK_NODE_KIND_STMT_DIV_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_SLASH_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_MOD_ASSIGN -> IDENT '%=' EXPR ';'
  [OAK_NODE_KIND_STMT_MOD_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_KIND_IDENT,
      OAK_TOKEN_PERCENT_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_KIND_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // ELSE_BLOCK -> 'else' '{' STMT* '}'
  [OAK_NODE_KIND_ELSE_BLOCK] = {
    .rules = {
      OAK_TOKEN_ELSE | OAK_RULE_TOKEN,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_KIND_STMT | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
};

int oak_node_grammar_op_unary(const oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_UNARY;
}

int oak_node_grammar_op_binary(const oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_BINARY;
}

static oak_ast_node_t* parse_rule(oak_parser_t* p, oak_node_kind_t kind);

static int try_skip_token(oak_parser_t* p, const oak_token_kind_t token_kind)
{
  if (p->curr == p->head)
    return 0;
  const oak_token_t* token = oak_container_of(p->curr, oak_token_t, link);
  if (token->kind != token_kind)
    return 0;
  p->curr = p->curr->next;
  return 1;
}

static size_t grammar_rule_count(const oak_grammar_entry_t* entry)
{
  size_t n = 0;
  while (n < OAK_ARRAY_SIZE(entry->rules) && entry->rules[n] != 0)
    ++n;
  return n;
}

static const oak_pratt_rule_t*
find_pratt_rule(const oak_pratt_rule_t* rules,
                const oak_token_kind_t token_kind)
{
  for (const oak_pratt_rule_t* r = rules; r->kind != OAK_PRATT_END; r++)
  {
    if (token_kind == r->trigger_token)
      return r;
  }
  return NULL;
}

static oak_ast_node_t* parse_token(oak_parser_t* p, const oak_node_kind_t kind)
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

static oak_ast_node_t* parse_rules(oak_parser_t* p, const oak_node_kind_t kind)
{
  oak_list_entry_t* saved = p->curr;
  const oak_grammar_entry_t* entry = &oak_grammar[kind];
  oak_list_head_t collected;
  oak_list_init(&collected);

  const size_t count = grammar_rule_count(entry);
  for (size_t i = 0; i < count; ++i)
  {
    const oak_rule_item_t rule = entry->rules[i];
    const int is_repeat = rule & OAK_RULE_REPEAT;
    const int is_optional = rule & OAK_RULE_OPTIONAL;

    if (rule & OAK_RULE_TOKEN)
    {
      const oak_token_kind_t tok =
          (oak_token_kind_t)(rule & OAK_RULE_KIND_MASK);
      if (is_repeat)
      {
        while (try_skip_token(p, tok))
        {
        }
        continue;
      }
      if (is_optional)
      {
        try_skip_token(p, tok);
        continue;
      }
      if (!try_skip_token(p, tok))
      {
        p->curr = saved;
        return NULL;
      }
      continue;
    }

    const oak_node_kind_t child_kind =
        (oak_node_kind_t)(rule & OAK_RULE_KIND_MASK);
    if (is_repeat)
    {
      for (;;)
      {
        oak_ast_node_t* child = parse_rule(p, child_kind);
        if (!child)
          break;
        oak_list_add_tail(&collected, &child->link);
      }
      continue;
    }

    oak_ast_node_t* child = parse_rule(p, child_kind);
    if (is_optional)
    {
      if (child)
        oak_list_add_tail(&collected, &child->link);
      continue;
    }
    if (!child)
    {
      p->curr = saved;
      return NULL;
    }
    oak_list_add_tail(&collected, &child->link);
  }

  const int is_binary = entry->op == OAK_GRAMMAR_BINARY;
  const int is_unary = entry->op == OAK_GRAMMAR_UNARY;
  if (is_binary && oak_list_length(&collected) < 2)
  {
    p->curr = saved;
    return NULL;
  }
  if (is_unary && oak_list_empty(&collected))
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

  if (is_binary)
  {
    oak_list_entry_t* lhs = oak_list_first(&collected);
    oak_list_entry_t* rhs = oak_list_next(lhs, &collected);
    node->lhs = oak_container_of(lhs, oak_ast_node_t, link);
    node->rhs = oak_container_of(rhs, oak_ast_node_t, link);
  }
  else if (is_unary)
  {
    oak_list_entry_t* first = oak_list_first(&collected);
    node->child = oak_container_of(first, oak_ast_node_t, link);
  }
  else
  {
    oak_list_move(&collected, &node->children);
  }

  return node;
}

static oak_ast_node_t* parse_choice(oak_parser_t* p, const oak_node_kind_t kind)
{
  oak_list_entry_t* saved = p->curr;
  const oak_grammar_entry_t* entry = &oak_grammar[kind];

  const size_t choice_count = grammar_rule_count(entry);
  for (size_t i = 0; i < choice_count; ++i)
  {
    oak_ast_node_t* child = parse_rule(p, (oak_node_kind_t)entry->rules[i]);
    if (child)
      return child;
  }

  p->curr = saved;
  return NULL;
}

static oak_ast_node_t*
parse_pratt(oak_parser_t* p, oak_node_kind_t kind, int min_bp);

static oak_ast_node_t* parse_pratt_unary(oak_parser_t* p,
                                         const oak_node_kind_t kind,
                                         const oak_pratt_rule_t* rule)
{
  oak_ast_node_t* operand = parse_pratt(p, kind, rule->r_bp);
  if (!operand)
    return NULL;
  oak_ast_node_t* node = oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
  if (!node)
    return NULL;
  node->kind = rule->node_kind;
  node->child = operand;
  return node;
}

static oak_ast_node_t* parse_pratt_binary(oak_parser_t* p,
                                          const oak_node_kind_t kind,
                                          const oak_pratt_rule_t* rule,
                                          oak_ast_node_t* lhs)
{
  oak_ast_node_t* rhs = parse_pratt(p, kind, rule->r_bp);
  if (!rhs)
    return NULL;
  oak_ast_node_t* node = oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
  if (!node)
    return NULL;
  node->kind = rule->node_kind;
  node->lhs = lhs;
  node->rhs = rhs;
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
      switch (r->kind)
      {
      case OAK_PRATT_GROUP:
        lhs = parse_pratt(p, kind, 0);
        if (!lhs || !try_skip_token(p, r->close_token))
          return NULL;
        break;
      default:
        lhs = parse_pratt_unary(p, kind, r);
        if (!lhs)
          return NULL;
        break;
      }
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
    if (!rule || rule->l_bp < min_bp)
      break;

    p->curr = p->curr->next;

    switch (rule->kind)
    {
    case OAK_PRATT_CALL:
    {
      oak_ast_node_t* call = oak_arena_alloc(p->arena, sizeof(oak_ast_node_t));
      if (!call)
        return NULL;
      call->kind = rule->node_kind;
      oak_list_init(&call->children);
      oak_list_add_tail(&call->children, &lhs->link);

      while (p->curr != p->head)
      {
        const oak_token_t* peek = oak_container_of(p->curr, oak_token_t, link);
        if (peek->kind == rule->close_token)
          break;
        oak_ast_node_t* arg = parse_rule(p, rule->arg_rule);
        if (!arg)
          return NULL;
        oak_list_add_tail(&call->children, &arg->link);
      }

      if (!try_skip_token(p, rule->close_token))
        return NULL;

      lhs = call;
      break;
    }
    default:
      lhs = parse_pratt_binary(p, kind, rule, lhs);
      if (!lhs)
        return NULL;
      break;
    }
  }

  return lhs;
}

static oak_ast_node_t* parse_rule(oak_parser_t* p, const oak_node_kind_t kind)
{
  if (kind == OAK_NODE_KIND_NONE)
    return NULL;
  if (p->curr == p->head)
    return NULL;

  switch (oak_grammar[kind].op)
  {
  case OAK_GRAMMAR_TOKEN:
    return parse_token(p, kind);
  case OAK_GRAMMAR_CHOICE:
    return parse_choice(p, kind);
  case OAK_GRAMMAR_PRATT:
    return parse_pratt(p, kind, 0);
  case OAK_GRAMMAR_BINARY:
  case OAK_GRAMMAR_UNARY:
  case OAK_GRAMMAR_SEQUENCE:
    return parse_rules(p, kind);
  }
  return NULL;
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
