#include "oak_test_token.h"

OAK_TEST_DECL(LexEmptyString)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("''");

  static struct oak_expected_token_t expected_tokens[] = {
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 1,
        .offset = 1,
        .string = "",
    },
  };

  const usize n = oak_count_of(expected_tokens);
  const enum oak_test_status_t result =
      oak_test_tokens(lexer, expected_tokens, n);
  oak_lexer_free(lexer);
  return result;
}

OAK_TEST_MAIN(LexEmptyString)
