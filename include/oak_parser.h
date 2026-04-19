#pragma once

#include "oak_lexer.h"
#include "oak_token.h"

enum oak_node_kind_t
{
  OAK_NODE_NONE,
  OAK_NODE_PROGRAM,
  OAK_NODE_PROGRAM_ITEM,
  OAK_NODE_STRUCT_DECL,
  OAK_NODE_TYPE_NAME,
  OAK_NODE_STRUCT_FIELD_DECL,
  OAK_NODE_ENUM_DECL,
  OAK_NODE_IDENT,
  OAK_NODE_STMT,
  OAK_NODE_STMT_EXPR,
  OAK_NODE_EXPR,
  OAK_NODE_INT,
  OAK_NODE_FLOAT,
  OAK_NODE_STRING,
  OAK_NODE_EXPR_PRIMARY,
  OAK_NODE_STMT_ASSIGNMENT,
  OAK_NODE_STMT_LET_ASSIGNMENT,
  OAK_NODE_BINARY_ADD,
  OAK_NODE_BINARY_SUB,
  OAK_NODE_BINARY_MUL,
  OAK_NODE_BINARY_DIV,
  OAK_NODE_BINARY_MOD,
  OAK_NODE_BINARY_EQ,
  OAK_NODE_BINARY_NEQ,
  OAK_NODE_BINARY_LESS,
  OAK_NODE_BINARY_LESS_EQ,
  OAK_NODE_BINARY_GREATER,
  OAK_NODE_BINARY_GREATER_EQ,
  OAK_NODE_BINARY_AND,
  OAK_NODE_BINARY_OR,
  OAK_NODE_UNARY_NEG,
  OAK_NODE_UNARY_NOT,
  OAK_NODE_FN_DECL,
  OAK_NODE_FN_RECEIVER,
  OAK_NODE_FN_PARAM,
  OAK_NODE_MUT_KEYWORD,
  OAK_NODE_FN_CALL,
  OAK_NODE_FN_CALL_ARG,
  OAK_NODE_STMT_RETURN,
  OAK_NODE_STMT_IF,
  OAK_NODE_ELSE_BLOCK,
  OAK_NODE_STMT_WHILE,
  OAK_NODE_STMT_FOR_FROM,
  OAK_NODE_STMT_FOR_IN,
  OAK_NODE_STMT_BREAK,
  OAK_NODE_STMT_CONTINUE,
  OAK_NODE_STMT_ADD_ASSIGN,
  OAK_NODE_STMT_SUB_ASSIGN,
  OAK_NODE_STMT_MUL_ASSIGN,
  OAK_NODE_STMT_DIV_ASSIGN,
  OAK_NODE_STMT_MOD_ASSIGN,
  OAK_NODE_MEMBER_ACCESS,
  OAK_NODE_TYPE_ARRAY,
  OAK_NODE_TYPE_MAP,
  OAK_NODE_EXPR_EMPTY_ARRAY,
  OAK_NODE_EXPR_EMPTY_MAP,
  OAK_NODE_EXPR_ARRAY_LITERAL,
  OAK_NODE_ARRAY_LITERAL_ELEMENT,
  OAK_NODE_EXPR_MAP_LITERAL,
  OAK_NODE_MAP_LITERAL_ENTRY,
  OAK_NODE_INDEX_ACCESS,
  OAK_NODE_EXPR_CAST,
  OAK_NODE_BLOCK,
};

struct oak_ast_node_t
{
  struct oak_list_entry_t link;
  enum oak_node_kind_t kind;

  union
  {
    const struct oak_token_t* token;
    struct oak_list_entry_t children;
    struct oak_ast_node_t* child;

    struct
    {
      struct oak_ast_node_t* lhs;
      struct oak_ast_node_t* rhs;
    };
  };
};

struct oak_parser_result_t;

struct oak_parser_result_t* oak_parse(const struct oak_lexer_result_t* lexer,
                                      enum oak_node_kind_t kind);
struct oak_ast_node_t*
oak_parser_root(const struct oak_parser_result_t* result);
void oak_parser_free(struct oak_parser_result_t* result);

int oak_node_is_unary_op(enum oak_node_kind_t kind);
int oak_node_is_binary_op(enum oak_node_kind_t kind);

void oak_ast_node_unpack(const struct oak_ast_node_t* node, ...);
