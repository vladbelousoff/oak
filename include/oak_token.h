#pragma once

#include "oak_list.h"
#include "oak_log.h"

typedef enum _oak_token_kind_t
{
  OAK_TOKEN_IDENT,
  OAK_TOKEN_TYPE,
  OAK_TOKEN_LPAREN,
  OAK_TOKEN_RPAREN,
  OAK_TOKEN_LBRACE,
  OAK_TOKEN_RBRACE,
  OAK_TOKEN_LBRACKET,
  OAK_TOKEN_RBRACKET,
  OAK_TOKEN_COMMA,
  OAK_TOKEN_SEMICOLON,
  OAK_TOKEN_COLON,
  OAK_TOKEN_EQUAL,
  OAK_TOKEN_NOT_EQUAL,
  OAK_TOKEN_EXCLAMATION_MARK,
  OAK_TOKEN_QUESTION_MARK,
  OAK_TOKEN_LESS,
  OAK_TOKEN_LESS_EQUAL,
  OAK_TOKEN_GREATER,
  OAK_TOKEN_GREATER_EQUAL,
  OAK_TOKEN_SLASH,
  OAK_TOKEN_PERCENT,
  OAK_TOKEN_ARROW,
  OAK_TOKEN_DOT,
  OAK_TOKEN_PLUS,
  OAK_TOKEN_MINUS,
  OAK_TOKEN_STAR,
  OAK_TOKEN_LET,
  OAK_TOKEN_MUT,
  OAK_TOKEN_IF,
  OAK_TOKEN_ELSE,
  OAK_TOKEN_WHILE,
  OAK_TOKEN_FOR,
  OAK_TOKEN_FROM,
  OAK_TOKEN_TO,
  OAK_TOKEN_BREAK,
  OAK_TOKEN_CONTINUE,
  OAK_TOKEN_RETURN,
  OAK_TOKEN_TRUE,
  OAK_TOKEN_FALSE,
  OAK_TOKEN_AND,
  OAK_TOKEN_OR,
  OAK_TOKEN_NOT,
  OAK_TOKEN_INT_NUM,
  OAK_TOKEN_FLOAT_NUM,
  OAK_TOKEN_STRING,
  OAK_TOKEN_ASSIGN,
  OAK_TOKEN_PLUS_ASSIGN,
  OAK_TOKEN_MINUS_ASSIGN,
  OAK_TOKEN_STAR_ASSIGN,
  OAK_TOKEN_SLASH_ASSIGN,
  OAK_TOKEN_PERCENT_ASSIGN,
  OAK_TOKEN_STRUCT,
  OAK_TOKEN_ENUM,
  OAK_TOKEN_FN,
} oak_token_kind_t;

typedef struct
{
  const char* kw;
  oak_token_kind_t kind;
} tea_kw_entry_t;

typedef struct _oak_token_t
{
  oak_list_entry_t link;
  oak_token_kind_t kind;
  int line;
  int column;
  int pos;
  int size;
  char buf[0];
} oak_token_t;

oak_token_kind_t oak_token_kind(const oak_token_t* token);
int oak_token_line(const oak_token_t* token);
int oak_token_column(const oak_token_t* token);
int oak_token_pos(const oak_token_t* token);
int oak_token_size(const oak_token_t* token);
const char* oak_token_buf(const oak_token_t* token);
int oak_token_as_i32(const oak_token_t* token);
float oak_token_as_f32(const oak_token_t* token);

oak_token_kind_t oak_ident_kind(const char* ident, size_t length);
const char* oak_token_name(oak_token_kind_t token_kind);
