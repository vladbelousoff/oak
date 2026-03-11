#pragma once

#include "oak_list.h"

typedef enum
{
  OAK_TOK_IDENT,
  OAK_TOK_LPAREN,
  OAK_TOK_RPAREN,
  OAK_TOK_LBRACE,
  OAK_TOK_RBRACE,
  OAK_TOK_LBRACKET,
  OAK_TOK_RBRACKET,
  OAK_TOK_COMMA,
  OAK_TOK_SEMICOLON,
  OAK_TOK_COLON,
  OAK_TOK_EQUAL,
  OAK_TOK_NOT_EQUAL,
  OAK_TOK_EXCLAMATION_MARK,
  OAK_TOK_QUESTION_MARK,
  OAK_TOK_LESS,
  OAK_TOK_LESS_EQUAL,
  OAK_TOK_GREATER,
  OAK_TOK_GREATER_EQUAL,
  OAK_TOK_SLASH,
  OAK_TOK_PERCENT,
  OAK_TOK_ARROW,
  OAK_TOK_DOT,
  OAK_TOK_PLUS,
  OAK_TOK_MINUS,
  OAK_TOK_STAR,
  OAK_TOK_LET,
  OAK_TOK_MUT,
  OAK_TOK_IF,
  OAK_TOK_ELSE,
  OAK_TOK_WHILE,
  OAK_TOK_FOR,
  OAK_TOK_BREAK,
  OAK_TOK_CONTINUE,
  OAK_TOK_RETURN,
  OAK_TOK_TRUE,
  OAK_TOK_FALSE,
  OAK_TOK_AND,
  OAK_TOK_OR,
  OAK_TOK_NOT,
  OAK_TOK_INT_NUM,
  OAK_TOK_FLOAT_NUM,
  OAK_TOK_STRING,
  OAK_TOK_ASSIGN,
  OAK_TOK_EOF,
} oak_tok_type_t;

typedef struct
{
  const char* kw;
  oak_tok_type_t type;
} tea_kw_entry_t;

typedef struct
{
  oak_list_entry_t link;
  oak_tok_type_t type;
  int line;
  int column;
  int pos;
  size_t size;
  char buf[0];
} oak_tok_t;

oak_tok_type_t oak_ident_type(const char* ident, int length);
const char* oak_tok_name(oak_tok_type_t token_type);
