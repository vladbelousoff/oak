#pragma once

#include "oak_lexer.h"
#include "oak_token.h"

enum oak_node_kind_t
{
  OAK_NODE_KIND_NONE,
  OAK_NODE_KIND_PROGRAM,
  OAK_NODE_KIND_PROGRAM_ITEM,
  OAK_NODE_KIND_STRUCT_DECL,
  OAK_NODE_KIND_TYPE_NAME,
  OAK_NODE_KIND_STRUCT_FIELD_DECL,
  OAK_NODE_KIND_ENUM_DECL,
  OAK_NODE_KIND_IDENT,
  OAK_NODE_KIND_STMT,
  OAK_NODE_KIND_STMT_EXPR,
  OAK_NODE_KIND_EXPR,
  OAK_NODE_KIND_INT,
  OAK_NODE_KIND_FLOAT,
  OAK_NODE_KIND_STRING,
  OAK_NODE_KIND_EXPR_PRIMARY,
  OAK_NODE_KIND_STMT_ASSIGNMENT,
  OAK_NODE_KIND_STMT_LET_ASSIGNMENT,
  OAK_NODE_KIND_BINARY_ADD,
  OAK_NODE_KIND_BINARY_SUB,
  OAK_NODE_KIND_BINARY_MUL,
  OAK_NODE_KIND_BINARY_DIV,
  OAK_NODE_KIND_BINARY_MOD,
  OAK_NODE_KIND_BINARY_EQ,
  OAK_NODE_KIND_BINARY_NEQ,
  OAK_NODE_KIND_BINARY_LESS,
  OAK_NODE_KIND_BINARY_LESS_EQ,
  OAK_NODE_KIND_BINARY_GREATER,
  OAK_NODE_KIND_BINARY_GREATER_EQ,
  OAK_NODE_KIND_BINARY_AND,
  OAK_NODE_KIND_BINARY_OR,
  OAK_NODE_KIND_UNARY_NEG,
  OAK_NODE_KIND_UNARY_NOT,
  OAK_NODE_KIND_FN_DECL,
  OAK_NODE_KIND_FN_RECEIVER,
  OAK_NODE_KIND_FN_PARAM,
  OAK_NODE_KIND_MUT_KEYWORD,
  OAK_NODE_KIND_FN_CALL,
  OAK_NODE_KIND_FN_CALL_ARG,
  OAK_NODE_KIND_STMT_RETURN,
  OAK_NODE_KIND_STMT_IF,
  OAK_NODE_KIND_ELSE_BLOCK,
  OAK_NODE_KIND_STMT_WHILE,
  OAK_NODE_KIND_STMT_FOR_FROM,
  OAK_NODE_KIND_STMT_BREAK,
  OAK_NODE_KIND_STMT_CONTINUE,
  OAK_NODE_KIND_STMT_ADD_ASSIGN,
  OAK_NODE_KIND_STMT_SUB_ASSIGN,
  OAK_NODE_KIND_STMT_MUL_ASSIGN,
  OAK_NODE_KIND_STMT_DIV_ASSIGN,
  OAK_NODE_KIND_STMT_MOD_ASSIGN,
  OAK_NODE_KIND_MEMBER_ACCESS,
  OAK_NODE_KIND_TYPE_ARRAY,
  OAK_NODE_KIND_TYPE_MAP,
  OAK_NODE_KIND_EXPR_EMPTY_ARRAY,
  OAK_NODE_KIND_EXPR_EMPTY_MAP,
  OAK_NODE_KIND_INDEX_ACCESS,
  OAK_NODE_KIND_BLOCK,
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
void oak_parser_cleanup(struct oak_parser_result_t* result);

int oak_node_grammar_op_unary(enum oak_node_kind_t kind);
int oak_node_grammar_op_binary(enum oak_node_kind_t kind);

void oak_ast_node_unpack(const struct oak_ast_node_t* node, ...);
