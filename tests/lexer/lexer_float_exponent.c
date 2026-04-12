#include "oak_test_token.h"

OAK_TEST_DECL(LexFloatExponent)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize("1e2 3.0E1 0.5e3");

  static struct oak_expected_token_t expected_tokens[] = {
    {
        .kind = OAK_TOKEN_FLOAT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .floating = 100.0f,
    },
    {
        .kind = OAK_TOKEN_FLOAT_NUM,
        .line = 1,
        .column = 5,
        .pos = 5,
        .floating = 30.0f,
    },
    {
        .kind = OAK_TOKEN_FLOAT_NUM,
        .line = 1,
        .column = 11,
        .pos = 11,
        .floating = 500.0f,
    },
  };

  const size_t n = OAK_ARRAY_SIZE(expected_tokens);
  const enum oak_result_t result = oak_test_tokens(lexer, expected_tokens, n);
  oak_lexer_cleanup(lexer);
  return result;
}

OAK_TEST_MAIN(LexFloatExponent)
