#include "oak_test_token.h"

OAK_TEST_DECL(LexArithmeticExpression)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("1 + 2 * 3");

  static oak_expected_token_t attrs[] = {
    {
        .kind = OAK_TOKEN_INT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .integer = 1,
    },
    {
        .kind = OAK_TOKEN_PLUS,
        .line = 1,
        .column = 3,
        .pos = 3,
    },
    {
        .kind = OAK_TOKEN_INT_NUM,
        .line = 1,
        .column = 5,
        .pos = 5,
        .integer = 2,
    },
    {
        .kind = OAK_TOKEN_STAR,
        .line = 1,
        .column = 7,
        .pos = 7,
    },
    {
        .kind = OAK_TOKEN_INT_NUM,
        .line = 1,
        .column = 9,
        .pos = 9,
        .integer = 3,
    },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lexer, attrs, n);
  oak_lexer_cleanup(lexer);

  return result;
}

OAK_TEST_MAIN(LexArithmeticExpression)
