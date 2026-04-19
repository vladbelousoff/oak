#include "oak_test_token.h"

OAK_TEST_DECL(LexWhitespaceAndNewlines)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("1 \n 2\r\t3");

  static struct oak_expected_token_t expected_tokens[] = {
    {
        .kind = OAK_TOKEN_INT,
        .line = 1,
        .column = 1,
        .offset = 1,
        .integer = 1,
    },
    {
        .kind = OAK_TOKEN_INT,
        .line = 2,
        .column = 2,
        .offset = 5,
        .integer = 2,
    },
    {
        .kind = OAK_TOKEN_INT,
        .line = 2,
        .column = 5,
        .offset = 8,
        .integer = 3,
    },
  };

  const usize n = oak_count_of(expected_tokens);
  const enum oak_test_status_t result =
      oak_test_tokens(lexer, expected_tokens, n);
  oak_lexer_free(lexer);
  return result;
}

OAK_TEST_MAIN(LexWhitespaceAndNewlines)
