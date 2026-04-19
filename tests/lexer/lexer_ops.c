#include "oak_test_token.h"

OAK_TEST_DECL(LexOperators)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("== != -> && || >= <= : , ; = ! - + * / ( ) { } [ ] > < . ?");

  static struct oak_expected_token_t expected_tokens[] = {
    { .kind = OAK_TOKEN_EQUAL_EQUAL, .line = 1, .column = 1, .offset = 1 },
    { .kind = OAK_TOKEN_BANG_EQUAL, .line = 1, .column = 4, .offset = 4 },
    { .kind = OAK_TOKEN_ARROW, .line = 1, .column = 7, .offset = 7 },
    { .kind = OAK_TOKEN_AND, .line = 1, .column = 10, .offset = 10 },
    { .kind = OAK_TOKEN_OR, .line = 1, .column = 13, .offset = 13 },
    { .kind = OAK_TOKEN_GREATER_EQUAL, .line = 1, .column = 16, .offset = 16 },
    { .kind = OAK_TOKEN_LESS_EQUAL, .line = 1, .column = 19, .offset = 19 },
    { .kind = OAK_TOKEN_COLON, .line = 1, .column = 22, .offset = 22 },
    { .kind = OAK_TOKEN_COMMA, .line = 1, .column = 24, .offset = 24 },
    { .kind = OAK_TOKEN_SEMICOLON, .line = 1, .column = 26, .offset = 26 },
    { .kind = OAK_TOKEN_ASSIGN, .line = 1, .column = 28, .offset = 28 },
    { .kind = OAK_TOKEN_BANG, .line = 1, .column = 30, .offset = 30 },
    { .kind = OAK_TOKEN_MINUS, .line = 1, .column = 32, .offset = 32 },
    { .kind = OAK_TOKEN_PLUS, .line = 1, .column = 34, .offset = 34 },
    { .kind = OAK_TOKEN_STAR, .line = 1, .column = 36, .offset = 36 },
    { .kind = OAK_TOKEN_SLASH, .line = 1, .column = 38, .offset = 38 },
    { .kind = OAK_TOKEN_LPAREN, .line = 1, .column = 40, .offset = 40 },
    { .kind = OAK_TOKEN_RPAREN, .line = 1, .column = 42, .offset = 42 },
    { .kind = OAK_TOKEN_LBRACE, .line = 1, .column = 44, .offset = 44 },
    { .kind = OAK_TOKEN_RBRACE, .line = 1, .column = 46, .offset = 46 },
    { .kind = OAK_TOKEN_LBRACKET, .line = 1, .column = 48, .offset = 48 },
    { .kind = OAK_TOKEN_RBRACKET, .line = 1, .column = 50, .offset = 50 },
    { .kind = OAK_TOKEN_GREATER, .line = 1, .column = 52, .offset = 52 },
    { .kind = OAK_TOKEN_LESS, .line = 1, .column = 54, .offset = 54 },
    { .kind = OAK_TOKEN_DOT, .line = 1, .column = 56, .offset = 56 },
    { .kind = OAK_TOKEN_QUESTION, .line = 1, .column = 58, .offset = 58 },
  };

  const usize n = oak_count_of(expected_tokens);
  const enum oak_test_status_t result =
      oak_test_tokens(lexer, expected_tokens, n);
  oak_lexer_free(lexer);
  return result;
}

OAK_TEST_MAIN(LexOperators)
