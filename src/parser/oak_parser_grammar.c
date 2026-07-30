#include "internal/oak_parser.h"

/* Pratt binding powers, lowest precedence first.  Within a binary level the
 * left binding power is even and the right is odd (left-associative); unary
 * and member-access use the same number for both sides where applicable.
 * The gaps (e.g. 16) leave room to insert intermediate levels later without
 * renumbering. */
enum
{
  OAK_BP_OR_L          = 1,
  OAK_BP_OR_R          = 2,
  OAK_BP_AND_L         = 3,
  OAK_BP_AND_R         = 4,
  OAK_BP_EQUALITY_L    = 5,
  OAK_BP_EQUALITY_R    = 6,
  OAK_BP_COMPARE_L     = 7,
  OAK_BP_COMPARE_R     = 8,
  OAK_BP_ADDITIVE_L    = 9,
  OAK_BP_ADDITIVE_R    = 10,
  OAK_BP_MULTIPLY_L    = 11,
  OAK_BP_MULTIPLY_R    = 12,
  OAK_BP_UNARY_R       = 13,
  OAK_BP_CAST_L        = 14,
  OAK_BP_POSTFIX_L     = 15,  /* call (), index [] */
  OAK_BP_MEMBER_L      = 17,
  OAK_BP_MEMBER_R      = 18,
};

static const struct oak_pratt_rule_t expr_prefix[] = {
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_MINUS,
      .r_bp = OAK_BP_UNARY_R,
      .node_kind = OAK_NODE_UNARY_NEG,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_BANG,
      .r_bp = OAK_BP_UNARY_R,
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
      .l_bp = OAK_BP_OR_L,
      .r_bp = OAK_BP_OR_R,
      .node_kind = OAK_NODE_BINARY_OR,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_AND,
      .l_bp = OAK_BP_AND_L,
      .r_bp = OAK_BP_AND_R,
      .node_kind = OAK_NODE_BINARY_AND,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_EQUAL_EQUAL,
      .l_bp = OAK_BP_EQUALITY_L,
      .r_bp = OAK_BP_EQUALITY_R,
      .node_kind = OAK_NODE_BINARY_EQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_BANG_EQUAL,
      .l_bp = OAK_BP_EQUALITY_L,
      .r_bp = OAK_BP_EQUALITY_R,
      .node_kind = OAK_NODE_BINARY_NEQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_LESS,
      .l_bp = OAK_BP_COMPARE_L,
      .r_bp = OAK_BP_COMPARE_R,
      .node_kind = OAK_NODE_BINARY_LESS,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_LESS_EQUAL,
      .l_bp = OAK_BP_COMPARE_L,
      .r_bp = OAK_BP_COMPARE_R,
      .node_kind = OAK_NODE_BINARY_LESS_EQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_GREATER,
      .l_bp = OAK_BP_COMPARE_L,
      .r_bp = OAK_BP_COMPARE_R,
      .node_kind = OAK_NODE_BINARY_GREATER,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_GREATER_EQUAL,
      .l_bp = OAK_BP_COMPARE_L,
      .r_bp = OAK_BP_COMPARE_R,
      .node_kind = OAK_NODE_BINARY_GREATER_EQ,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_PLUS,
      .l_bp = OAK_BP_ADDITIVE_L,
      .r_bp = OAK_BP_ADDITIVE_R,
      .node_kind = OAK_NODE_BINARY_ADD,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_MINUS,
      .l_bp = OAK_BP_ADDITIVE_L,
      .r_bp = OAK_BP_ADDITIVE_R,
      .node_kind = OAK_NODE_BINARY_SUB,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_STAR,
      .l_bp = OAK_BP_MULTIPLY_L,
      .r_bp = OAK_BP_MULTIPLY_R,
      .node_kind = OAK_NODE_BINARY_MUL,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_SLASH,
      .l_bp = OAK_BP_MULTIPLY_L,
      .r_bp = OAK_BP_MULTIPLY_R,
      .node_kind = OAK_NODE_BINARY_DIV,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_DOUBLE_SLASH,
      .l_bp = OAK_BP_MULTIPLY_L,
      .r_bp = OAK_BP_MULTIPLY_R,
      .node_kind = OAK_NODE_BINARY_INT_DIV,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_PERCENT,
      .l_bp = OAK_BP_MULTIPLY_L,
      .r_bp = OAK_BP_MULTIPLY_R,
      .node_kind = OAK_NODE_BINARY_MOD,
  },
  {
      .kind = OAK_PRATT_CALL,
      .trigger_token = OAK_TOKEN_LPAREN,
      .l_bp = OAK_BP_POSTFIX_L,
      .node_kind = OAK_NODE_FN_CALL,
      .close_token = OAK_TOKEN_RPAREN,
      .arg_rule = OAK_NODE_FN_CALL_ARG,
  },
  {
      .kind = OAK_PRATT_INDEX,
      .trigger_token = OAK_TOKEN_LBRACKET,
      .l_bp = OAK_BP_POSTFIX_L,
      .node_kind = OAK_NODE_INDEX_ACCESS,
      .close_token = OAK_TOKEN_RBRACKET,
  },
  {
      .kind = OAK_PRATT_OP,
      .trigger_token = OAK_TOKEN_DOT,
      .l_bp = OAK_BP_MEMBER_L,
      .r_bp = OAK_BP_MEMBER_R,
      .node_kind = OAK_NODE_MEMBER_ACCESS,
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
  // PROGRAM_ITEM -> ATTR_DECL | EXPORT_DECL | IMPORT_DECL | IMPORT_SELECTIVE | IMPORT_WILDCARD | METHOD_DECL | FN_DECL | RECORD_DECL_EMPTY | RECORD_DECL | ENUM_DECL | INTERFACE_DECL | STMT
  [OAK_NODE_PROGRAM_ITEM] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_ATTR_DECL,
      OAK_NODE_EXPORT_DECL,
      OAK_NODE_IMPORT_DECL,
      OAK_NODE_IMPORT_SELECTIVE,
      OAK_NODE_IMPORT_WILDCARD,
      OAK_NODE_METHOD_DECL,
      OAK_NODE_FN_DECL,
      OAK_NODE_RECORD_DECL_EMPTY,
      OAK_NODE_RECORD_DECL,
      OAK_NODE_ENUM_DECL,
      OAK_NODE_INTERFACE_DECL,
      OAK_NODE_STMT,
    },
  },
  // IMPORT_DECL -> 'import' IMPORT_PATH 'as' IDENT ';'
  //   (binary: lhs = IMPORT_PATH, rhs = alias IDENT or null)
  [OAK_NODE_IMPORT_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_IMPORT | OAK_RULE_TOKEN,
      OAK_NODE_IMPORT_PATH,
      OAK_TOKEN_AS     | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // IMPORT_PATH -> IDENT ('.' IDENT)*  (encoded as a single dot-separated repeat)
  [OAK_NODE_IMPORT_PATH] = {
    .rules = {
      OAK_NODE_IDENT | OAK_RULE_REPEAT | OAK_RULE_DOT_SEP,
    },
  },
  // RECORD_DECL_EMPTY -> 'record' TYPE_NAME ';'
  //   (unary: child = TYPE_NAME)
  [OAK_NODE_RECORD_DECL_EMPTY] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_RECORD | OAK_RULE_TOKEN,
      OAK_NODE_TYPE_NAME,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // RECORD_DECL -> 'record' TYPE_NAME '{' RECORD_FIELDS '}'
  //   (binary: lhs = TYPE_NAME, rhs = RECORD_FIELDS)
  [OAK_NODE_RECORD_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_RECORD | OAK_RULE_TOKEN,
      OAK_NODE_TYPE_NAME,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_RECORD_FIELDS,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // RECORD_FIELDS -> RECORD_FIELD_DECL*
  [OAK_NODE_RECORD_FIELDS] = {
    .rules = {
      OAK_NODE_RECORD_FIELD_DECL | OAK_RULE_REPEAT,
    },
  },
  // RECORD_FIELD_DECL -> IDENT ':' TYPE_NAME ';'
  [OAK_NODE_RECORD_FIELD_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_IDENT,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_NODE_TYPE_NAME,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // ENUM_DECL -> 'enum' IDENT '{' ENUM_VARIANTS '}'
  //   (binary: lhs = IDENT, rhs = ENUM_VARIANTS)
  [OAK_NODE_ENUM_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_ENUM | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_ENUM_VARIANTS,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // ENUM_VARIANTS -> IDENT (',' IDENT)*   (commas optional, trailing comma ok)
  [OAK_NODE_ENUM_VARIANTS] = {
    .rules = {
      OAK_NODE_IDENT | OAK_RULE_REPEAT | OAK_RULE_COMMA_SEP,
    },
  },
  // TYPE_NAME -> TYPE_WEAK | TYPE_ARRAY | TYPE_MAP | TYPE_FN | IDENT
  [OAK_NODE_TYPE_NAME] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_TYPE_WEAK,
      OAK_NODE_TYPE_ARRAY,
      OAK_NODE_TYPE_MAP,
      OAK_NODE_TYPE_FN,
      OAK_NODE_IDENT,
    },
  },
  // TYPE_WEAK -> (TYPE_ARRAY | TYPE_MAP | IDENT) 'weak'
  [OAK_NODE_TYPE_WEAK] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_NODE_TYPE_WEAK_BASE,
      OAK_TOKEN_WEAK | OAK_RULE_TOKEN,
    },
  },
  [OAK_NODE_TYPE_WEAK_BASE] = {
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
      OAK_NODE_TYPE_ARRAY_BASE,
      OAK_TOKEN_LBRACKET | OAK_RULE_TOKEN,
      OAK_TOKEN_RBRACKET | OAK_RULE_TOKEN,
    },
  },
  [OAK_NODE_TYPE_ARRAY_BASE] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_IDENT,
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
  // EXPR_PRIMARY -> INT | FLOAT | STRING
  //               | EXPR_MAP_LITERAL | EXPR_ARRAY_LITERAL
  //               | EXPR_NEW_ARRAY | EXPR_NEW_MAP
  //               | EXPR_RECORD_LITERAL | IDENT
  [OAK_NODE_EXPR_PRIMARY] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_INT,
      OAK_NODE_FLOAT,
      OAK_NODE_STRING,
      OAK_NODE_TRUE,
      OAK_NODE_FALSE,
      OAK_NODE_NONE_LITERAL,
      OAK_NODE_EXPR_MAP_LITERAL,
      OAK_NODE_EXPR_ARRAY_LITERAL,
      OAK_NODE_EXPR_NEW_ARRAY,
      OAK_NODE_EXPR_NEW_MAP,
      OAK_NODE_EXPR_RECORD_LITERAL,
      OAK_NODE_EXPR_FN,
      OAK_NODE_SELF,
      OAK_NODE_IDENT,
    },
  },
  // EXPR_RECORD_LITERAL -> 'new' IMPORT_PATH '{' RECORD_LITERAL_FIELDS '}'
  //   lhs = IMPORT_PATH (1 child = local type; 2 children = module.TypeName)
  //   rhs = RECORD_LITERAL_FIELDS
  [OAK_NODE_EXPR_RECORD_LITERAL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_NEW | OAK_RULE_TOKEN,
      OAK_NODE_IMPORT_PATH,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_RECORD_LITERAL_FIELDS,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // RECORD_LITERAL_FIELDS -> RECORD_LITERAL_FIELD*
  [OAK_NODE_RECORD_LITERAL_FIELDS] = {
    .rules = {
      OAK_NODE_RECORD_LITERAL_FIELD | OAK_RULE_REPEAT,
    },
  },
  // RECORD_LITERAL_FIELD -> IDENT (':' EXPR)? ','?
  [OAK_NODE_RECORD_LITERAL_FIELD] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_IDENT,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
      OAK_NODE_EXPR | OAK_RULE_OPTIONAL,
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
  // EXPR_MAP_LITERAL -> '[' MAP_LITERAL_ENTRY MAP_LITERAL_ENTRIES ']'
  // (binary: first entry + zero-or-more additional entries; needed for two
  //  parsed nonterminals under OAK_GRAMMAR_BINARY.)
  [OAK_NODE_EXPR_MAP_LITERAL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_LBRACKET | OAK_RULE_TOKEN,
      OAK_NODE_MAP_LITERAL_ENTRY,
      OAK_NODE_MAP_LITERAL_ENTRIES,
      OAK_TOKEN_RBRACKET | OAK_RULE_TOKEN,
    },
  },
  // MAP_LITERAL_ENTRIES -> MAP_LITERAL_ENTRY*
  [OAK_NODE_MAP_LITERAL_ENTRIES] = {
    .rules = {
      OAK_NODE_MAP_LITERAL_ENTRY | OAK_RULE_REPEAT,
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
  // EXPR_NEW_ARRAY -> 'new' TYPE_ARRAY
  [OAK_NODE_EXPR_NEW_ARRAY] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_NEW | OAK_RULE_TOKEN,
      OAK_NODE_TYPE_ARRAY,
    },
  },
  // EXPR_NEW_MAP -> 'new' TYPE_MAP
  [OAK_NODE_EXPR_NEW_MAP] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_NEW | OAK_RULE_TOKEN,
      OAK_NODE_TYPE_MAP,
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
  [OAK_NODE_TRUE] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_TRUE,
  },
  [OAK_NODE_FALSE] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_FALSE,
  },
  [OAK_NODE_NONE_LITERAL] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_NONE,
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
  //   (binary: lhs = MUT_KEYWORD?, rhs = STMT_ASSIGNMENT)
  [OAK_NODE_STMT_LET_ASSIGNMENT] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_LET | OAK_RULE_TOKEN,
      OAK_NODE_MUT_KEYWORD | OAK_RULE_OPTIONAL,
      OAK_NODE_STMT_ASSIGNMENT,
    },
  },
  // FN_DECL -> FN_PROTO FN_DECL_BODY (binary: lhs = signature, rhs = body/semicolon)
  [OAK_NODE_FN_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_FN_PROTO,
      OAK_NODE_FN_DECL_BODY,
    },
  },
  // FN_DECL_BODY -> BLOCK | ';'
  [OAK_NODE_FN_DECL_BODY] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_BLOCK,
      OAK_NODE_FN_DECL_SEMICOLON,
    },
  },
  [OAK_NODE_FN_DECL_SEMICOLON] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_SEMICOLON,
  },
  // FN_PROTO -> FN_HEAD FN_PARAMS_AND_RET (binary: lhs = name intro, rhs = params + return)
  [OAK_NODE_FN_PROTO] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_FN_HEAD,
      OAK_NODE_FN_PARAMS_AND_RET,
    },
  },
  // FN_HEAD -> FN_PREFIX FN_NAME
  //   (binary: lhs = 'fn' keyword, rhs = IDENT)
  [OAK_NODE_FN_HEAD] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_FN_PREFIX,
      OAK_NODE_FN_NAME,
    },
  },
  // FN_PREFIX -> 'fn' (token leaf, same pattern as IDENT)
  [OAK_NODE_FN_PREFIX] = {
    .op = OAK_GRAMMAR_TOKEN,
    .token_kind = OAK_TOKEN_FN,
  },
  // FN_PARAMS_AND_RET -> FN_PARAM_LIST FN_RETURN_TYPE?
  //   (binary: lhs = FN_PARAM_LIST, rhs = FN_RETURN_TYPE?)
  [OAK_NODE_FN_PARAMS_AND_RET] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_FN_PARAM_LIST,
      OAK_NODE_FN_RETURN_TYPE | OAK_RULE_OPTIONAL,
    },
  },
  // FN_PARAM_LIST -> '(' FN_PARAM_SELF? FN_PARAMS ')'
  //   (binary: lhs = FN_PARAM_SELF?, rhs = FN_PARAMS)
  [OAK_NODE_FN_PARAM_LIST] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_LPAREN | OAK_RULE_TOKEN,
      OAK_NODE_FN_PARAM_SELF | OAK_RULE_OPTIONAL,
      OAK_NODE_FN_PARAMS,
      OAK_TOKEN_RPAREN | OAK_RULE_TOKEN,
    },
  },
  // FN_PARAMS -> FN_PARAM*
  [OAK_NODE_FN_PARAMS] = {
    .rules = {
      OAK_NODE_FN_PARAM | OAK_RULE_REPEAT,
    },
  },
  // FN_RETURN_TYPE -> '->' TYPE_NAME (unary: child = TYPE_NAME)
  [OAK_NODE_FN_RETURN_TYPE] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_ARROW | OAK_RULE_TOKEN,
      OAK_NODE_TYPE_NAME,
    },
  },
  // FN_PARAM -> MUT_KEYWORD? IDENT ':' TYPE_NAME ','?
  [OAK_NODE_FN_PARAM] = {
    .rules = {
      OAK_NODE_MUT_KEYWORD | OAK_RULE_OPTIONAL,
      OAK_NODE_IDENT,
      OAK_TOKEN_COLON | OAK_RULE_TOKEN,
      OAK_NODE_TYPE_NAME,
      OAK_TOKEN_COMMA | OAK_RULE_TOKEN | OAK_RULE_OPTIONAL,
    },
  },
  // FN_PARAM_SELF -> MUT_KEYWORD? 'self' ','?
  //   (binary: lhs = MUT_KEYWORD?, rhs = SELF)
  [OAK_NODE_FN_PARAM_SELF] = {
    .op = OAK_GRAMMAR_BINARY,
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
  [OAK_NODE_BINARY_INT_DIV]    = { .op = OAK_GRAMMAR_BINARY },
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
  // STMT_RETURN -> 'return' EXPR? ';' (unary: child = EXPR?)
  [OAK_NODE_STMT_RETURN] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_RETURN | OAK_RULE_TOKEN,
      OAK_NODE_EXPR | OAK_RULE_OPTIONAL,
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
  // INTERFACE_DECL -> 'interface' IDENT '{' INTERFACE_MEMBERS '}'
  //   (binary: lhs = IDENT (name), rhs = INTERFACE_MEMBERS)
  [OAK_NODE_INTERFACE_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_INTERFACE | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_INTERFACE_MEMBERS,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // INTERFACE_MEMBERS -> FN_DECL*  (body may be ';' for abstract or BLOCK for default)
  [OAK_NODE_INTERFACE_MEMBERS] = {
    .rules = {
      OAK_NODE_FN_DECL | OAK_RULE_REPEAT,
    },
  },
  // IMPL_DECL -> 'impl' IDENT '{' IMPL_MEMBERS '}'
  //   (binary: lhs = IDENT (type name), rhs = IMPL_MEMBERS)
  [OAK_NODE_IMPL_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_IMPL | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_IMPL_MEMBERS,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
    },
  },
  // IMPL_MEMBERS -> FN_DECL*
  [OAK_NODE_IMPL_MEMBERS] = {
    .rules = {
      OAK_NODE_FN_DECL | OAK_RULE_REPEAT,
    },
  },
  // METHOD_DECL -> METHOD_PROTO FN_DECL_BODY
  //   (binary: lhs = METHOD_PROTO, rhs = FN_DECL_BODY)
  [OAK_NODE_METHOD_DECL] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_METHOD_PROTO,
      OAK_NODE_FN_DECL_BODY,
    },
  },
  // METHOD_PROTO -> METHOD_HEAD FN_PARAMS_AND_RET
  //   (binary: lhs = METHOD_HEAD, rhs = FN_PARAMS_AND_RET)
  [OAK_NODE_METHOD_PROTO] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_METHOD_HEAD,
      OAK_NODE_FN_PARAMS_AND_RET,
    },
  },
  // METHOD_HEAD -> 'fn' IDENT '.' IDENT
  //   (binary: lhs = type IDENT, rhs = method IDENT)
  [OAK_NODE_METHOD_HEAD] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_FN | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
      OAK_TOKEN_DOT | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
    },
  },
  // ATTR -> '@' IDENT
  //   (unary: child = IDENT — the attribute name)
  [OAK_NODE_ATTR] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_AT | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
    },
  },
  // ATTR_DECL -> ATTR ATTR* (EXPORT_DECL | METHOD_DECL | FN_DECL | RECORD_DECL_EMPTY | RECORD_DECL | ENUM_DECL | INTERFACE_DECL)
  //   Sequence node; children: one or more ATTR nodes followed by the declaration node.
  //   The required first ATTR ensures ATTR_DECL fails immediately if no '@' is present,
  //   preventing it from accidentally consuming plain declarations.
  [OAK_NODE_ATTR_DECL] = {
    .rules = {
      OAK_NODE_ATTR,                    /* required: at least one @Attr */
      OAK_NODE_ATTR | OAK_RULE_REPEAT,  /* zero or more additional @Attrs */
      OAK_NODE_ATTR_DECL_BODY,          /* the actual declaration */
    },
  },
  // ATTR_DECL_BODY -> EXPORT_DECL | METHOD_DECL | FN_DECL | RECORD_DECL_EMPTY | RECORD_DECL | ENUM_DECL | INTERFACE_DECL
  //   Transparent choice — returns the matched declaration node directly.
  [OAK_NODE_ATTR_DECL_BODY] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_EXPORT_DECL,
      OAK_NODE_METHOD_DECL,
      OAK_NODE_FN_DECL,
      OAK_NODE_RECORD_DECL_EMPTY,
      OAK_NODE_RECORD_DECL,
      OAK_NODE_ENUM_DECL,
      OAK_NODE_INTERFACE_DECL,
    },
  },
  // EXPORT_DECL -> 'export' (METHOD_DECL | FN_DECL | RECORD_DECL_EMPTY | RECORD_DECL | ENUM_DECL | INTERFACE_DECL)
  //   Unary: child = exported declaration. Attributes go before export:
  //   @Attr export fn ...
  [OAK_NODE_EXPORT_DECL] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_EXPORT | OAK_RULE_TOKEN,
      OAK_NODE_EXPORT_DECL_BODY,
    },
  },
  // EXPORT_DECL_BODY -> METHOD_DECL | FN_DECL | RECORD_DECL_EMPTY | RECORD_DECL | ENUM_DECL | INTERFACE_DECL
  //   Transparent choice — returns the matched declaration node directly.
  [OAK_NODE_EXPORT_DECL_BODY] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_METHOD_DECL,
      OAK_NODE_FN_DECL,
      OAK_NODE_RECORD_DECL_EMPTY,
      OAK_NODE_RECORD_DECL,
      OAK_NODE_ENUM_DECL,
      OAK_NODE_INTERFACE_DECL,
    },
  },
  // IMPORT_SELECTIVE -> 'import' '{' IMPORT_NAMES '}' 'from' IMPORT_PATH ';'
  //   (binary: lhs = IMPORT_NAMES, rhs = IMPORT_PATH)
  [OAK_NODE_IMPORT_SELECTIVE] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_IMPORT | OAK_RULE_TOKEN,
      OAK_TOKEN_LBRACE | OAK_RULE_TOKEN,
      OAK_NODE_IMPORT_NAMES,
      OAK_TOKEN_RBRACE | OAK_RULE_TOKEN,
      OAK_TOKEN_FROM   | OAK_RULE_TOKEN,
      OAK_NODE_IMPORT_PATH,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // IMPORT_WILDCARD -> 'import' '*' 'from' IMPORT_PATH ';'
  //   (unary: child = IMPORT_PATH)
  [OAK_NODE_IMPORT_WILDCARD] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_IMPORT | OAK_RULE_TOKEN,
      OAK_TOKEN_STAR   | OAK_RULE_TOKEN,
      OAK_TOKEN_FROM   | OAK_RULE_TOKEN,
      OAK_NODE_IMPORT_PATH,
      OAK_TOKEN_SEMICOLON | OAK_RULE_TOKEN,
    },
  },
  // IMPORT_NAMES -> IMPORT_NAME (',' IMPORT_NAME)*
  [OAK_NODE_IMPORT_NAMES] = {
    .rules = {
      OAK_NODE_IMPORT_NAME | OAK_RULE_REPEAT | OAK_RULE_COMMA_SEP,
    },
  },
  // IMPORT_NAME -> IDENT IMPORT_ALIAS?
  //   (binary: lhs = original name, rhs = alias node or null)
  [OAK_NODE_IMPORT_NAME] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_NODE_IDENT,
      OAK_NODE_IMPORT_ALIAS | OAK_RULE_OPTIONAL,
    },
  },
  // IMPORT_ALIAS -> 'as' IDENT  (both required as a unit)
  [OAK_NODE_IMPORT_ALIAS] = {
    .op = OAK_GRAMMAR_UNARY,
    .rules = {
      OAK_TOKEN_AS | OAK_RULE_TOKEN,
      OAK_NODE_IDENT,
    },
  },
  // FN_NAME -> IDENT (restricted choice for function heads)
  [OAK_NODE_FN_NAME] = {
    .op = OAK_GRAMMAR_CHOICE,
    .rules = {
      OAK_NODE_IDENT,
    },
  },
  // EXPR_FN -> 'fn' FN_PARAMS_AND_RET BLOCK
  //   (binary: lhs = FN_PARAMS_AND_RET, rhs = BLOCK)
  [OAK_NODE_EXPR_FN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_FN | OAK_RULE_TOKEN,
      OAK_NODE_FN_PARAMS_AND_RET,
      OAK_NODE_BLOCK,
    },
  },
  // TYPE_FN -> 'fn' '(' TYPE_FN_PARAMS ')' FN_RETURN_TYPE?
  //   (binary: lhs = TYPE_FN_PARAMS, rhs = FN_RETURN_TYPE?)
  [OAK_NODE_TYPE_FN] = {
    .op = OAK_GRAMMAR_BINARY,
    .rules = {
      OAK_TOKEN_FN | OAK_RULE_TOKEN,
      OAK_TOKEN_LPAREN | OAK_RULE_TOKEN,
      OAK_NODE_TYPE_FN_PARAMS,
      OAK_TOKEN_RPAREN | OAK_RULE_TOKEN,
      OAK_NODE_FN_RETURN_TYPE | OAK_RULE_OPTIONAL,
    },
  },
  // TYPE_FN_PARAMS -> TYPE_NAME*  (comma-separated)
  [OAK_NODE_TYPE_FN_PARAMS] = {
    .rules = {
      OAK_NODE_TYPE_NAME | OAK_RULE_REPEAT | OAK_RULE_COMMA_SEP,
    },
  },
};
