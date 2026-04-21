#include "oak_parser_internal.h"

static const struct oak_pratt_rule_t expr_prefix[] = {
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_MINUS,
      .r_bp = 13,
      .node_kind = OAK_NODE_UNARY_NEG,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_BANG,
      .r_bp = 13,
      .node_kind = OAK_NODE_UNARY_NOT,
  },
  {
      .kind = OAK_PRATT_GROUP,
      .trigger_token = OAK_TOKEN_LPAREN,
      .close_token = OAK_TOKEN_RPAREN,
  },
  { 0 },
};

static const struct oak_pratt_rule_t expr_infix[] = {
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_OR,
      .l_bp = 1,
      .r_bp = 2,
      .node_kind = OAK_NODE_BINARY_OR,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_AND,
      .l_bp = 3,
      .r_bp = 4,
      .node_kind = OAK_NODE_BINARY_AND,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_EQUAL_EQUAL,
      .l_bp = 5,
      .r_bp = 6,
      .node_kind = OAK_NODE_BINARY_EQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_BANG_EQUAL,
      .l_bp = 5,
      .r_bp = 6,
      .node_kind = OAK_NODE_BINARY_NEQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_LESS,
      .l_bp = 7,
      .r_bp = 8,
      .node_kind = OAK_NODE_BINARY_LESS,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_LESS_EQUAL,
      .l_bp = 7,
      .r_bp = 8,
      .node_kind = OAK_NODE_BINARY_LESS_EQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_GREATER,
      .l_bp = 7,
      .r_bp = 8,
      .node_kind = OAK_NODE_BINARY_GREATER,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_GREATER_EQUAL,
      .l_bp = 7,
      .r_bp = 8,
      .node_kind = OAK_NODE_BINARY_GREATER_EQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_PLUS,
      .l_bp = 9,
      .r_bp = 10,
      .node_kind = OAK_NODE_BINARY_ADD,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_MINUS,
      .l_bp = 9,
      .r_bp = 10,
      .node_kind = OAK_NODE_BINARY_SUB,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_STAR,
      .l_bp = 11,
      .r_bp = 12,
      .node_kind = OAK_NODE_BINARY_MUL,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_SLASH,
      .l_bp = 11,
      .r_bp = 12,
      .node_kind = OAK_NODE_BINARY_DIV,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_PERCENT,
      .l_bp = 11,
      .r_bp = 12,
      .node_kind = OAK_NODE_BINARY_MOD,
  },
  {
      .kind = OAK_PRATT_CALL,
      .trigger_token = OAK_TOKEN_LPAREN,
      .l_bp = 15,
      .node_kind = OAK_NODE_FN_CALL,
      .close_token = OAK_TOKEN_RPAREN,
      .arg_rule = OAK_NODE_FN_CALL_ARG,
  },
  {
      .kind = OAK_PRATT_INDEX,
      .trigger_token = OAK_TOKEN_LBRACKET,
      .l_bp = 15,
      .node_kind = OAK_NODE_INDEX_ACCESS,
      .close_token = OAK_TOKEN_RBRACKET,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_DOT,
      .l_bp = 17,
      .r_bp = 18,
      .node_kind = OAK_NODE_MEMBER_ACCESS,
  },
  {
      .kind = OAK_PRATT_CAST,
      .trigger_token = OAK_TOKEN_AS,
      .l_bp = 14,
      .node_kind = OAK_NODE_EXPR_CAST,
  },
  { 0 },
};

struct oak_grammar_entry_t oak_grammar[] = {
  // PROGRAM -> PROGRAM_ITEM*
  [OAK_NODE_PROGRAM] = {
    .rules = {
      OAK_NODE_PROGRAM_ITEM | OAK_RULE_REPEAT,
    },
  },
  // PROGRAM_ITEM -> FN_DECL | STRUCT_DECL | ENUM_DECL | STMT
  [OAK_NODE_PROGRAM_ITEM] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_FN_DECL,
      OAK_NODE_STRUCT_DECL,
      OAK_NODE_ENUM_DECL,
      OAK_NODE_STMT,
    },
  },
  // STRUCT_DECL -> 'type' TYPE_NAME 'struct' '{' STRUCT_FIELDS '}'
  //   (binary: lhs = TYPE_NAME, rhs = STRUCT_FIELDS)
  [OAK_NODE_STRUCT_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_TYPE | OAK_RULE_TOKEN,
      OAK_NODE_TYPE_NAME,
      OAK_TOKEN_STRUCT | OAK_RULE_TOKEN,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_STRUCT_FIELDS,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // STRUCT_FIELDS -> STRUCT_FIELD_DECL*
  [OAK_NODE_STRUCT_FIELDS] = {
    .rules = {
      OAK_NODE_STRUCT_FIELD_DECL | OAK_RULE_REPEAT,
    },
  },
  // STRUCT_FIELD_DECL -> IDENT ':' IDENT ';'
  [OAK_NODE_STRUCT_FIELD_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_IDENT,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // ENUM_DECL -> 'type' IDENT 'enum' '{' ENUM_VARIANTS '}'
  //   (binary: lhs = IDENT, rhs = ENUM_VARIANTS)
  [OAK_NODE_ENUM_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_TYPE | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_ENUM | OAK_RULE_TOKEN,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_ENUM_VARIANTS,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // ENUM_VARIANTS -> IDENT*
  [OAK_NODE_ENUM_VARIANTS] = {
    .rules = {
      OAK_NODE_IDENT | OAK_RULE_REPEAT,
    },
  },
  // TYPE_NAME -> TYPE_ARRAY | TYPE_MAP | IDENT
  [OAK_NODE_TYPE_NAME] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_TYPE_ARRAY,
      OAK_NODE_TYPE_MAP,
      OAK_NODE_IDENT,
    },
  },
  // TYPE_ARRAY -> IDENT '[' ']'
  [OAK_NODE_TYPE_ARRAY] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_NODE_IDENT,
      OAK_TOKEN_LBRACKET | OAK_RULE_TOKEN,
      OAK_TOKEN_RBRACKET | OAK_RULE_TOKEN,
    },
  },
  // TYPE_MAP -> '[' IDENT ':' IDENT ']'
  [OAK_NODE_TYPE_MAP] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_LBRACKET | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_RBRACKET | OAK_RULE_TOKEN,
    },
  },
  [OAK_NODE_IDENT] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_IDENT,
  },
  // SELF -> 'self' (used as a primary expression and inside FN_PARAM_SELF).
  [OAK_NODE_SELF] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_SELF,
  },
  // STMT -> STMT_IF | STMT_WHILE | STMT_FOR_FROM | STMT_BREAK | STMT_CONTINUE
  //       | STMT_RETURN | STMT_LET_ASSIGNMENT | STMT_ASSIGNMENT | STMT_EXPR
  [OAK_NODE_STMT] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_STMT_IF,
      OAK_NODE_STMT_WHILE,
      OAK_NODE_STMT_FOR_FROM,
      OAK_NODE_STMT_FOR_IN,
      OAK_NODE_STMT_BREAK,
      OAK_NODE_STMT_CONTINUE,
      OAK_NODE_STMT_RETURN,
      OAK_NODE_STMT_LET_ASSIGNMENT,
      OAK_NODE_STMT_ASSIGNMENT,
      OAK_NODE_STMT_ADD_ASSIGN,
      OAK_NODE_STMT_SUB_ASSIGN,
      OAK_NODE_STMT_MUL_ASSIGN,
      OAK_NODE_STMT_DIV_ASSIGN,
      OAK_NODE_STMT_MOD_ASSIGN,
      OAK_NODE_STMT_EXPR,
    },
  },
  // STMT_EXPR -> EXPR ';'
  [OAK_NODE_STMT_EXPR] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  [OAK_NODE_EXPR] = {
    .op = OAK_GRAMMAR_PRATT,
    .pratt = {
      .primary_rule = OAK_NODE_EXPR_PRIMARY,
      .prefix = expr_prefix,
      .infix = expr_infix,
    },
  },
  // EXPR_PRIMARY -> INT | FLOAT | STRING | '[]' | '[:]'
  //               | EXPR_MAP_LITERAL | EXPR_ARRAY_LITERAL
  //               | EXPR_STRUCT_LITERAL | IDENT
  [OAK_NODE_EXPR_PRIMARY] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_INT,
      OAK_NODE_FLOAT,
      OAK_NODE_STRING,
      OAK_NODE_EXPR_EMPTY_ARRAY,
      OAK_NODE_EXPR_EMPTY_MAP,
      OAK_NODE_EXPR_MAP_LITERAL,
      OAK_NODE_EXPR_ARRAY_LITERAL,
      OAK_NODE_EXPR_STRUCT_LITERAL,
      OAK_NODE_SELF,
      OAK_NODE_IDENT,
    },
  },
  // EXPR_STRUCT_LITERAL -> 'new' IDENT '{' STRUCT_LITERAL_FIELDS '}'
  [OAK_NODE_EXPR_STRUCT_LITERAL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_NEW | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_STRUCT_LITERAL_FIELDS,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // STRUCT_LITERAL_FIELDS -> STRUCT_LITERAL_FIELD*
  [OAK_NODE_STRUCT_LITERAL_FIELDS] = {
    .rules = {
      OAK_NODE_STRUCT_LITERAL_FIELD | OAK_RULE_REPEAT,
    },
  },
  // STRUCT_LITERAL_FIELD -> IDENT ':' EXPR ','?
  [OAK_NODE_STRUCT_LITERAL_FIELD] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_IDENT,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
    },
  },
  // EXPR_EMPTY_ARRAY -> '[' ']'
  [OAK_NODE_EXPR_EMPTY_ARRAY] = {
    .rules = {
      OAK_TOKEN_LBRACKET | OAK_RULE_TOKEN,
      OAK_TOKEN_RBRACKET | OAK_RULE_TOKEN,
    },
  },
  // EXPR_EMPTY_MAP -> '[' ':' ']'
  [OAK_NODE_EXPR_EMPTY_MAP] = {
    .rules = {
      OAK_TOKEN_LBRACKET | OAK_RULE_TOKEN,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_TOKEN_RBRACKET | OAK_RULE_TOKEN,
    },
  },
  // EXPR_ARRAY_LITERAL -> '[' ARRAY_LITERAL_ELEMENT+ ']'
  [OAK_NODE_EXPR_ARRAY_LITERAL] = {
    .rules = {
      OAK_TOKEN_LBRACKET | OAK_RULE_TOKEN,
      OAK_NODE_ARRAY_LITERAL_ELEMENT,
      OAK_NODE_ARRAY_LITERAL_ELEMENT | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACKET | OAK_RULE_TOKEN,
    },
  },
  // ARRAY_LITERAL_ELEMENT -> EXPR ','?
  [OAK_NODE_ARRAY_LITERAL_ELEMENT] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
    },
  },
  // EXPR_MAP_LITERAL -> '[' MAP_LITERAL_ENTRY+ ']'
  [OAK_NODE_EXPR_MAP_LITERAL] = {
    .rules = {
      OAK_TOKEN_LBRACKET | OAK_RULE_TOKEN,
      OAK_NODE_MAP_LITERAL_ENTRY,
      OAK_NODE_MAP_LITERAL_ENTRY | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACKET | OAK_RULE_TOKEN,
    },
  },
  // MAP_LITERAL_ENTRY -> EXPR ':' EXPR ','?
  [OAK_NODE_MAP_LITERAL_ENTRY] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
    },
  },
  [OAK_NODE_INT] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_INT,
  },
  [OAK_NODE_FLOAT] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_FLOAT,
  },
  [OAK_NODE_STRING] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_STRING,
  },
  // STMT_ASSIGNMENT -> EXPR '=' EXPR ';'
  [OAK_NODE_STMT_ASSIGNMENT] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_LET_ASSIGNMENT -> 'let' MUT_KEYWORD? STMT_ASSIGNMENT
  [OAK_NODE_STMT_LET_ASSIGNMENT] = {
    .rules = {
      OAK_TOKEN_LET | OAK_RULE_TOKEN,
      OAK_NODE_MUT_KEYWORD | OAK_RULE_OPTIONAL,
      OAK_NODE_STMT_ASSIGNMENT,
    },
  },
  // FN_DECL -> 'fn' FN_RECEIVER? IDENT '(' FN_PARAM_SELF? FN_PARAM* ')'
  //            ('->' TYPE_NAME)? BLOCK
  [OAK_NODE_FN_DECL] = {
    .rules = {
      OAK_TOKEN_FN | OAK_RULE_TOKEN,
      OAK_NODE_FN_RECEIVER | OAK_RULE_OPTIONAL,
      OAK_NODE_IDENT,
      OAK_TOKEN_LPAREN | OAK_RULE_TOKEN,
      OAK_NODE_FN_PARAM_SELF | OAK_RULE_OPTIONAL,
      OAK_NODE_FN_PARAM | OAK_RULE_REPEAT,
      OAK_TOKEN_RPAREN | OAK_RULE_TOKEN,
      OAK_TOKEN_ARROW | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
      OAK_NODE_TYPE_NAME | OAK_RULE_OPTIONAL,
      OAK_NODE_BLOCK,
    },
  },
  // FN_RECEIVER -> IDENT '.'
  [OAK_NODE_FN_RECEIVER] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_NODE_IDENT,
      OAK_TOKEN_DOT | OAK_RULE_TOKEN,
    },
  },
  // FN_PARAM -> MUT_KEYWORD? IDENT ':' IDENT ','?
  [OAK_NODE_FN_PARAM] = {
    .rules = {
      OAK_NODE_MUT_KEYWORD | OAK_RULE_OPTIONAL,
      OAK_NODE_IDENT,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
    },
  },
  // FN_PARAM_SELF -> MUT_KEYWORD? 'self' ','?
  [OAK_NODE_FN_PARAM_SELF] = {
    .rules = {
      OAK_NODE_MUT_KEYWORD | OAK_RULE_OPTIONAL,
      OAK_NODE_SELF,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
    },
  },
  [OAK_NODE_MUT_KEYWORD] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_MUT,
  },
  [OAK_NODE_BINARY_ADD]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_SUB]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_MUL]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_DIV]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_MOD]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_EQ]         = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_NEQ]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_LESS]       = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_LESS_EQ]    = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_GREATER]    = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_GREATER_EQ] = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_AND]        = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_BINARY_OR]         = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_UNARY_NEG]         = { .op = OAK_GRAMMAR_UNARY },
  [OAK_NODE_UNARY_NOT]         = { .op = OAK_GRAMMAR_UNARY },
  [OAK_NODE_MEMBER_ACCESS]     = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_INDEX_ACCESS]      = { .op = OAK_GRAMMAR_BINARY },
  [OAK_NODE_EXPR_CAST]         = { .op = OAK_GRAMMAR_BINARY },
  // FN_CALL_ARG -> EXPR ','?
  [OAK_NODE_FN_CALL_ARG] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
    },
  },
  // STMT_RETURN -> 'return' EXPR ';'
  [OAK_NODE_STMT_RETURN] = {
    .rules = {
      OAK_TOKEN_RETURN | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_IF -> 'if' EXPR BLOCK ELSE_BLOCK?
  [OAK_NODE_STMT_IF] = {
    .rules = {
      OAK_TOKEN_IF | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_NODE_BLOCK,
      OAK_NODE_ELSE_BLOCK | OAK_RULE_OPTIONAL,
    },
  },
  // STMT_WHILE -> 'while' EXPR BLOCK
  [OAK_NODE_STMT_WHILE] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_WHILE | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_NODE_BLOCK,
    },
  },
  // STMT_FOR_FROM -> 'for' IDENT 'from' EXPR 'to' EXPR BLOCK
  [OAK_NODE_STMT_FOR_FROM] = {
    .rules = {
      OAK_TOKEN_FOR | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_FROM | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_TO | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_NODE_BLOCK,
    },
  },
  // STMT_FOR_IN -> 'for' IDENT (',' IDENT)? 'in' EXPR BLOCK
  [OAK_NODE_STMT_FOR_IN] = {
    .rules = {
      OAK_TOKEN_FOR | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
      OAK_NODE_IDENT | OAK_RULE_OPTIONAL,
      OAK_TOKEN_IN | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_NODE_BLOCK,
    },
  },
  // STMT_BREAK -> 'break' ';'
  [OAK_NODE_STMT_BREAK] = {
    .rules = {
      OAK_TOKEN_BREAK | OAK_RULE_TOKEN,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_CONTINUE -> 'continue' ';'
  [OAK_NODE_STMT_CONTINUE] = {
    .rules = {
      OAK_TOKEN_CONTINUE | OAK_RULE_TOKEN,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_ADD_ASSIGN -> EXPR '+=' EXPR ';'
  [OAK_NODE_STMT_ADD_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_PLUS_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_SUB_ASSIGN -> EXPR '-=' EXPR ';'
  [OAK_NODE_STMT_SUB_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_MINUS_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_MUL_ASSIGN -> EXPR '*=' EXPR ';'
  [OAK_NODE_STMT_MUL_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_STAR_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_DIV_ASSIGN -> EXPR '/=' EXPR ';'
  [OAK_NODE_STMT_DIV_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_SLASH_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // STMT_MOD_ASSIGN -> EXPR '%=' EXPR ';'
  [OAK_NODE_STMT_MOD_ASSIGN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_EXPR,
      OAK_TOKEN_PERCENT_ASSIGN | OAK_RULE_TOKEN,
      OAK_NODE_EXPR,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // ELSE_BLOCK -> 'else' BLOCK
  [OAK_NODE_ELSE_BLOCK] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_ELSE | OAK_RULE_TOKEN,
      OAK_NODE_BLOCK,
    },
  },
  // BLOCK -> '{' STMT* '}'
  [OAK_NODE_BLOCK] = {
    .rules = {
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_STMT | OAK_RULE_REPEAT,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
};
