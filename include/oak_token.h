#pragma once

#include "oak_export.h"
#include "oak_list.h"
#include "oak_log.h"

typedef enum oak_token_kind oak_token_kind_t;
enum oak_token_kind
{
  OAK_TOKEN_IDENT,
  OAK_TOKEN_LPAREN,
  OAK_TOKEN_RPAREN,
  OAK_TOKEN_LBRACE,
  OAK_TOKEN_RBRACE,
  OAK_TOKEN_LBRACKET,
  OAK_TOKEN_RBRACKET,
  OAK_TOKEN_COMMA,
  OAK_TOKEN_SEMICOLON,
  OAK_TOKEN_COLON,
  OAK_TOKEN_EQUAL_EQUAL,
  OAK_TOKEN_BANG_EQUAL,
  OAK_TOKEN_BANG,
  OAK_TOKEN_QUESTION,
  OAK_TOKEN_LESS,
  OAK_TOKEN_LESS_EQUAL,
  OAK_TOKEN_GREATER,
  OAK_TOKEN_GREATER_EQUAL,
  OAK_TOKEN_SLASH,
  OAK_TOKEN_DOUBLE_SLASH,
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
  OAK_TOKEN_IN,
  OAK_TOKEN_BREAK,
  OAK_TOKEN_CONTINUE,
  OAK_TOKEN_RETURN,
  OAK_TOKEN_TRUE,
  OAK_TOKEN_FALSE,
  OAK_TOKEN_AND,
  OAK_TOKEN_OR,
  OAK_TOKEN_INT,
  OAK_TOKEN_FLOAT,
  OAK_TOKEN_STRING,
  OAK_TOKEN_ASSIGN,
  OAK_TOKEN_PLUS_ASSIGN,
  OAK_TOKEN_MINUS_ASSIGN,
  OAK_TOKEN_STAR_ASSIGN,
  OAK_TOKEN_SLASH_ASSIGN,
  OAK_TOKEN_PERCENT_ASSIGN,
  OAK_TOKEN_RECORD,
  OAK_TOKEN_ENUM,
  OAK_TOKEN_FN,
  OAK_TOKEN_AS,
  OAK_TOKEN_NEW,
  OAK_TOKEN_SELF,
  OAK_TOKEN_IMPORT,
  OAK_TOKEN_EXPORT,
  OAK_TOKEN_INTERFACE,
  OAK_TOKEN_IMPL,
  OAK_TOKEN_WEAK,
  OAK_TOKEN_NONE,
  OAK_TOKEN_AT,
};

typedef struct oak_keyword_entry oak_keyword_entry_t;
struct oak_keyword_entry
{
  const char* keyword;
  oak_token_kind_t kind;
};

typedef struct oak_token oak_token_t;
struct oak_token
{
  oak_list_entry_t link;
  oak_token_kind_t kind;
  int line;
  int column;
  int offset;
  /* Lexeme length in bytes. */
  int length;
  char text[];
};

OAK_API oak_token_kind_t oak_token_kind(const oak_token_t* token);
OAK_API int oak_token_line(const oak_token_t* token);
OAK_API int oak_token_column(const oak_token_t* token);
OAK_API int oak_token_offset(const oak_token_t* token);
OAK_API int oak_token_size(const oak_token_t* token);
OAK_API const char* oak_token_text(const oak_token_t* token);
OAK_API int oak_token_as_i32(const oak_token_t* token);
OAK_API float oak_token_as_f32(const oak_token_t* token);

OAK_API oak_token_kind_t oak_keyword_lookup(const char* ident);
OAK_API const char* oak_token_name(oak_token_kind_t token_kind);
