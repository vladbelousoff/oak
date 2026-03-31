#include "oak_test_token.h"

OAK_TEST_DECL(LexWhitespaceAndNewlines)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("1 \n 2\r\t3");

  static oak_token_attr_t attrs[] = {
    {
        .kind = OAK_TOKEN_INT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .i_val = 1,
    },
    {
        .kind = OAK_TOKEN_INT_NUM,
        .line = 2,
        .column = 2,
        .pos = 5,
        .i_val = 2,
    },
    {
        .kind = OAK_TOKEN_INT_NUM,
        .line = 2,
        .column = 5,
        .pos = 8,
        .i_val = 3,
    },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lexer, attrs, n);
  oak_lexer_cleanup(lexer);
  return result;
}

OAK_TEST_MAIN(LexWhitespaceAndNewlines)
