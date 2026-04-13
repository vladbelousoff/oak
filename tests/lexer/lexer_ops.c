#include "oak_test_token.h"

OAK_TEST_DECL(LexOperators)
{
  struct oak_lexer_result_t* lexer =
      OAK_LEX("== != -> && || >= <= : , ; = ! - + * / ( ) { } [ ] > < . ?");

  static struct oak_expected_token_t expected_tokens[] = {
    { .kind = OAK_TOKEN_EQUAL, .line = 1, .column = 1, .pos = 1 },
    { .kind = OAK_TOKEN_NOT_EQUAL, .line = 1, .column = 4, .pos = 4 },
    { .kind = OAK_TOKEN_ARROW, .line = 1, .column = 7, .pos = 7 },
    { .kind = OAK_TOKEN_AND, .line = 1, .column = 10, .pos = 10 },
    { .kind = OAK_TOKEN_OR, .line = 1, .column = 13, .pos = 13 },
    { .kind = OAK_TOKEN_GREATER_EQUAL, .line = 1, .column = 16, .pos = 16 },
    { .kind = OAK_TOKEN_LESS_EQUAL, .line = 1, .column = 19, .pos = 19 },
    { .kind = OAK_TOKEN_COLON, .line = 1, .column = 22, .pos = 22 },
    { .kind = OAK_TOKEN_COMMA, .line = 1, .column = 24, .pos = 24 },
    { .kind = OAK_TOKEN_SEMICOLON, .line = 1, .column = 26, .pos = 26 },
    { .kind = OAK_TOKEN_ASSIGN, .line = 1, .column = 28, .pos = 28 },
    { .kind = OAK_TOKEN_EXCLAMATION_MARK, .line = 1, .column = 30, .pos = 30 },
    { .kind = OAK_TOKEN_MINUS, .line = 1, .column = 32, .pos = 32 },
    { .kind = OAK_TOKEN_PLUS, .line = 1, .column = 34, .pos = 34 },
    { .kind = OAK_TOKEN_STAR, .line = 1, .column = 36, .pos = 36 },
    { .kind = OAK_TOKEN_SLASH, .line = 1, .column = 38, .pos = 38 },
    { .kind = OAK_TOKEN_LPAREN, .line = 1, .column = 40, .pos = 40 },
    { .kind = OAK_TOKEN_RPAREN, .line = 1, .column = 42, .pos = 42 },
    { .kind = OAK_TOKEN_LBRACE, .line = 1, .column = 44, .pos = 44 },
    { .kind = OAK_TOKEN_RBRACE, .line = 1, .column = 46, .pos = 46 },
    { .kind = OAK_TOKEN_LBRACKET, .line = 1, .column = 48, .pos = 48 },
    { .kind = OAK_TOKEN_RBRACKET, .line = 1, .column = 50, .pos = 50 },
    { .kind = OAK_TOKEN_GREATER, .line = 1, .column = 52, .pos = 52 },
    { .kind = OAK_TOKEN_LESS, .line = 1, .column = 54, .pos = 54 },
    { .kind = OAK_TOKEN_DOT, .line = 1, .column = 56, .pos = 56 },
    { .kind = OAK_TOKEN_QUESTION_MARK, .line = 1, .column = 58, .pos = 58 },
  };

  const size_t n = oak_count_of(expected_tokens);
  const enum oak_test_status_t result =
      oak_test_tokens(lexer, expected_tokens, n);
  oak_lexer_cleanup(lexer);
  return result;
}

OAK_TEST_MAIN(LexOperators)
