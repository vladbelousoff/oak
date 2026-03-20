#include "oak_lexer.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_token.h"

OAK_TEST_DECL(LexOperators)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "== != -> && || >= <= : , ; = ! - + * / ( ) { } [ ] > < . ?");

  static oak_token_attr_t attrs[] = {
    { .type = OAK_TOKEN_EQUAL, .line = 1, .column = 1, .pos = 1 },
    { .type = OAK_TOKEN_NOT_EQUAL, .line = 1, .column = 4, .pos = 4 },
    { .type = OAK_TOKEN_ARROW, .line = 1, .column = 7, .pos = 7 },
    { .type = OAK_TOKEN_AND, .line = 1, .column = 10, .pos = 10 },
    { .type = OAK_TOKEN_OR, .line = 1, .column = 13, .pos = 13 },
    { .type = OAK_TOKEN_GREATER_EQUAL, .line = 1, .column = 16, .pos = 16 },
    { .type = OAK_TOKEN_LESS_EQUAL, .line = 1, .column = 19, .pos = 19 },
    { .type = OAK_TOKEN_COLON, .line = 1, .column = 22, .pos = 22 },
    { .type = OAK_TOKEN_COMMA, .line = 1, .column = 24, .pos = 24 },
    { .type = OAK_TOKEN_SEMICOLON, .line = 1, .column = 26, .pos = 26 },
    { .type = OAK_TOKEN_ASSIGN, .line = 1, .column = 28, .pos = 28 },
    { .type = OAK_TOKEN_EXCLAMATION_MARK, .line = 1, .column = 30, .pos = 30 },
    { .type = OAK_TOKEN_MINUS, .line = 1, .column = 32, .pos = 32 },
    { .type = OAK_TOKEN_PLUS, .line = 1, .column = 34, .pos = 34 },
    { .type = OAK_TOKEN_STAR, .line = 1, .column = 36, .pos = 36 },
    { .type = OAK_TOKEN_SLASH, .line = 1, .column = 38, .pos = 38 },
    { .type = OAK_TOKEN_LPAREN, .line = 1, .column = 40, .pos = 40 },
    { .type = OAK_TOKEN_RPAREN, .line = 1, .column = 42, .pos = 42 },
    { .type = OAK_TOKEN_LBRACE, .line = 1, .column = 44, .pos = 44 },
    { .type = OAK_TOKEN_RBRACE, .line = 1, .column = 46, .pos = 46 },
    { .type = OAK_TOKEN_LBRACKET, .line = 1, .column = 48, .pos = 48 },
    { .type = OAK_TOKEN_RBRACKET, .line = 1, .column = 50, .pos = 50 },
    { .type = OAK_TOKEN_GREATER, .line = 1, .column = 52, .pos = 52 },
    { .type = OAK_TOKEN_LESS, .line = 1, .column = 54, .pos = 54 },
    { .type = OAK_TOKEN_DOT, .line = 1, .column = 56, .pos = 56 },
    { .type = OAK_TOKEN_QUESTION_MARK, .line = 1, .column = 58, .pos = 58 },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lexer, attrs, n);
  oak_lexer_cleanup(lexer);
  return result;
}

OAK_TEST_MAIN(LexOperators)
