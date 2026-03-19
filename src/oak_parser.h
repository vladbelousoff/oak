#pragma once

#include "oak_lex.h"
#include "oak_tok.h"

typedef enum
{
  OAK_NODE_KIND_NONE,
  OAK_NODE_KIND_PROGRAM,
  OAK_NODE_KIND_PROGRAM_ITEM,
  OAK_NODE_KIND_TYPE_DECL,
  OAK_NODE_KIND_TYPE_KEYWORD,
  OAK_NODE_KIND_TYPE_NAME,
  OAK_NODE_KIND_TYPE_FIELD_DECLS,
  OAK_NODE_KIND_TYPE_FIELD_DECL,
  OAK_NODE_KIND_LBRACE,
  OAK_NODE_KIND_RBRACE,
  OAK_NODE_KIND_IDENT,
  OAK_NODE_KIND_COLON,
  OAK_NODE_KIND_SEMICOLON,
  OAK_NODE_KIND_STATEMENT,
  OAK_NODE_KIND_EXPR,
  OAK_NODE_KIND_INT,
  OAK_NODE_KIND_FLOAT,
  OAK_NODE_KIND_STRING,
  OAK_NODE_KIND_EXPR_PRIMARY,
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
} oak_node_kind_t;

int oak_node_kind_is_token(oak_node_kind_t kind);

typedef struct _oak_ast_node_t
{
  oak_list_entry_t link;
  oak_node_kind_t kind;

  union
  {
    const oak_tok_t* tok;
    oak_list_head_t children;
  };
} oak_ast_node_t;

oak_ast_node_t* oak_parse(const oak_lex_t* lex, oak_node_kind_t kind);
void oak_ast_node_cleanup(oak_ast_node_t* node);
