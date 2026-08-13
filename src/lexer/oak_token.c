#include "oak_token.h"

#include "oak_count_of.h"
#include "oak_log.h"

#include <string.h>

oak_token_kind_t oak_token_kind(const oak_token_t* token)
{
  return token->kind;
}

int oak_token_line(const oak_token_t* token)
{
  return token->line;
}

int oak_token_column(const oak_token_t* token)
{
  return token->column;
}

int oak_token_offset(const oak_token_t* token)
{
  return token->offset;
}

int oak_token_size(const oak_token_t* token)
{
  return token->length;
}

const char* oak_token_text(const oak_token_t* token)
{
  return token->text;
}

int oak_token_as_i32(const oak_token_t* token)
{
  oak_assert(token->kind == OAK_TOKEN_INT);
  return *(const int*)token->text;
}

float oak_token_as_f32(const oak_token_t* token)
{
  oak_assert(token->kind == OAK_TOKEN_FLOAT);
  return *(const float*)token->text;
}

oak_token_kind_t oak_keyword_lookup(const char* ident)
{
  static const oak_keyword_entry_t keywords[] = {
    { "as", OAK_TOKEN_AS },
    { "break", OAK_TOKEN_BREAK },   { "continue", OAK_TOKEN_CONTINUE },
    { "else", OAK_TOKEN_ELSE },     { "enum", OAK_TOKEN_ENUM },
    { "export", OAK_TOKEN_EXPORT }, { "false", OAK_TOKEN_FALSE },
    { "fn", OAK_TOKEN_FN },
    { "for", OAK_TOKEN_FOR },       { "from", OAK_TOKEN_FROM },
    { "if", OAK_TOKEN_IF },         { "import", OAK_TOKEN_IMPORT },
    { "in", OAK_TOKEN_IN },         { "let", OAK_TOKEN_LET },
    { "mut", OAK_TOKEN_MUT },       { "new", OAK_TOKEN_NEW },
    { "return", OAK_TOKEN_RETURN }, { "self", OAK_TOKEN_SELF },
    { "record", OAK_TOKEN_RECORD }, { "to", OAK_TOKEN_TO },
    { "interface", OAK_TOKEN_INTERFACE },   { "impl", OAK_TOKEN_IMPL },
    { "none", OAK_TOKEN_NONE },      { "weak", OAK_TOKEN_WEAK },
    { "true", OAK_TOKEN_TRUE },     { "while", OAK_TOKEN_WHILE },
  };

  for (int i = 0; i < (int)oak_count_of(keywords); ++i)
  {
    const char* keyword = keywords[i].keyword;
    if (strcmp(ident, keyword) == 0)
      return keywords[i].kind;
  }

  return OAK_TOKEN_IDENT;
}

const char* oak_token_name(const oak_token_kind_t token_kind)
{
  switch (token_kind)
  {
    case OAK_TOKEN_IDENT:
      return "IDENT";
    case OAK_TOKEN_LPAREN:
      return "LPAREN";
    case OAK_TOKEN_RPAREN:
      return "RPAREN";
    case OAK_TOKEN_LBRACE:
      return "LBRACE";
    case OAK_TOKEN_RBRACE:
      return "RBRACE";
    case OAK_TOKEN_LBRACKET:
      return "LBRACKET";
    case OAK_TOKEN_RBRACKET:
      return "RBRACKET";
    case OAK_TOKEN_COMMA:
      return "COMMA";
    case OAK_TOKEN_SEMICOLON:
      return "SEMICOLON";
    case OAK_TOKEN_COLON:
      return "COLON";
    case OAK_TOKEN_EQUAL_EQUAL:
      return "EQUAL_EQUAL";
    case OAK_TOKEN_BANG_EQUAL:
      return "BANG_EQUAL";
    case OAK_TOKEN_BANG:
      return "BANG";
    case OAK_TOKEN_QUESTION:
      return "QUESTION";
    case OAK_TOKEN_LESS:
      return "LESS";
    case OAK_TOKEN_LESS_EQUAL:
      return "LESS_EQUAL";
    case OAK_TOKEN_GREATER:
      return "GREATER";
    case OAK_TOKEN_GREATER_EQUAL:
      return "GREATER_EQUAL";
    case OAK_TOKEN_SLASH:
      return "SLASH";
    case OAK_TOKEN_DOUBLE_SLASH:
      return "DOUBLE_SLASH";
    case OAK_TOKEN_PERCENT:
      return "PERCENT";
    case OAK_TOKEN_ARROW:
      return "ARROW";
    case OAK_TOKEN_DOT:
      return "DOT";
    case OAK_TOKEN_PLUS:
      return "PLUS";
    case OAK_TOKEN_MINUS:
      return "MINUS";
    case OAK_TOKEN_STAR:
      return "STAR";
    case OAK_TOKEN_LET:
      return "LET";
    case OAK_TOKEN_MUT:
      return "MUT";
    case OAK_TOKEN_IF:
      return "IF";
    case OAK_TOKEN_ELSE:
      return "ELSE";
    case OAK_TOKEN_WHILE:
      return "WHILE";
    case OAK_TOKEN_FOR:
      return "FOR";
    case OAK_TOKEN_FROM:
      return "FROM";
    case OAK_TOKEN_TO:
      return "TO";
    case OAK_TOKEN_IN:
      return "IN";
    case OAK_TOKEN_BREAK:
      return "BREAK";
    case OAK_TOKEN_CONTINUE:
      return "CONTINUE";
    case OAK_TOKEN_RETURN:
      return "RETURN";
    case OAK_TOKEN_TRUE:
      return "TRUE";
    case OAK_TOKEN_FALSE:
      return "FALSE";
    case OAK_TOKEN_AND:
      return "AND";
    case OAK_TOKEN_OR:
      return "OR";
    case OAK_TOKEN_INT:
      return "INT";
    case OAK_TOKEN_FLOAT:
      return "FLOAT";
    case OAK_TOKEN_STRING:
      return "STRING";
    case OAK_TOKEN_ASSIGN:
      return "ASSIGN";
    case OAK_TOKEN_PLUS_ASSIGN:
      return "PLUS_ASSIGN";
    case OAK_TOKEN_MINUS_ASSIGN:
      return "MINUS_ASSIGN";
    case OAK_TOKEN_STAR_ASSIGN:
      return "STAR_ASSIGN";
    case OAK_TOKEN_SLASH_ASSIGN:
      return "SLASH_ASSIGN";
    case OAK_TOKEN_PERCENT_ASSIGN:
      return "PERCENT_ASSIGN";
    case OAK_TOKEN_RECORD:
      return "RECORD";
    case OAK_TOKEN_ENUM:
      return "ENUM";
    case OAK_TOKEN_FN:
      return "FN";
    case OAK_TOKEN_AS:
      return "AS";
    case OAK_TOKEN_NEW:
      return "NEW";
    case OAK_TOKEN_SELF:
      return "SELF";
    case OAK_TOKEN_IMPORT:
      return "IMPORT";
    case OAK_TOKEN_EXPORT:
      return "EXPORT";
    case OAK_TOKEN_INTERFACE:
      return "INTERFACE";
    case OAK_TOKEN_IMPL:
      return "IMPL";
    case OAK_TOKEN_WEAK:
      return "WEAK";
    case OAK_TOKEN_NONE:
      return "NONE";
    case OAK_TOKEN_AT:
      return "@";
    default:
      return null;
  }
}
