#include "oak_test_token.h"

OAK_TEST_DECL(LexInteger)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("1000");

  static struct oak_expected_token_t expected_tokens[] = {
    {
        .kind = OAK_TOKEN_INT,
        .line = 1,
        .column = 1,
        .offset = 1,
        .integer = 1000,
    },
  };

  const enum oak_test_status_t result =
      oak_test_tokens(lexer, expected_tokens, 1);
  oak_lexer_free(lexer);

  return result;
}

OAK_TEST_MAIN(LexInteger)
