#include "oak_token.h"

#include "oak_common.h"

#include <string.h>

oak_token_type_t oak_ident_type(const char* ident, const size_t length)
{
  static const tea_kw_entry_t keywords[] = {
    { "and", OAK_TOKEN_AND },
    { "break", OAK_TOKEN_BREAK },
    { "continue", OAK_TOKEN_CONTINUE },
    { "else", OAK_TOKEN_ELSE },
    { "false", OAK_TOKEN_FALSE },
    { "for", OAK_TOKEN_FOR },
    { "if", OAK_TOKEN_IF },
    { "let", OAK_TOKEN_LET },
    { "mut", OAK_TOKEN_MUT },
    { "not", OAK_TOKEN_NOT },
    { "or", OAK_TOKEN_OR },
    { "return", OAK_TOKEN_RETURN },
    { "true", OAK_TOKEN_TRUE },
    { "type", OAK_TOKEN_TYPE },
    { "while", OAK_TOKEN_WHILE },
  };

  for (int i = 0; i < OAK_ARRAY_SIZE(keywords); ++i)
  {
    const char* kw = keywords[i].kw;
    if (strncmp(ident, kw, length + 1) == 0)
      return keywords[i].type;
  }

  return OAK_TOKEN_IDENT;
}

const char* oak_token_name(const oak_token_type_t token_type)
{
  switch (token_type)
  {
  case OAK_TOKEN_IDENT:
    return "IDENT";
  case OAK_TOKEN_TYPE:
    return "TYPE";
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
  case OAK_TOKEN_EQUAL:
    return "EQUAL";
  case OAK_TOKEN_NOT_EQUAL:
    return "NOT_EQUAL";
  case OAK_TOKEN_EXCLAMATION_MARK:
    return "EXCLAMATION_MARK";
  case OAK_TOKEN_QUESTION_MARK:
    return "QUESTION_MARK";
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
  case OAK_TOKEN_NOT:
    return "NOT";
  case OAK_TOKEN_INT_NUM:
    return "INT_NUM";
  case OAK_TOKEN_FLOAT_NUM:
    return "FLOAT_NUM";
  case OAK_TOKEN_STRING:
    return "STRING";
  case OAK_TOKEN_ASSIGN:
    return "ASSIGN";
  case OAK_TOKEN_EOF:
    return "EOF";
  default:
    return NULL;
  }
}