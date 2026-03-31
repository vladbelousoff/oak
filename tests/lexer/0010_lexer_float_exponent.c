#include "oak_test_token.h"

OAK_TEST_DECL(LexFloatExponent)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("1e2 3.0E1 0.5e3");

  static oak_token_attr_t attrs[] = {
    {
        .kind = OAK_TOKEN_FLOAT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .f_val = 100.0f,
    },
    {
        .kind = OAK_TOKEN_FLOAT_NUM,
        .line = 1,
        .column = 5,
        .pos = 5,
        .f_val = 30.0f,
    },
    {
        .kind = OAK_TOKEN_FLOAT_NUM,
        .line = 1,
        .column = 11,
        .pos = 11,
        .f_val = 500.0f,
    },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lexer, attrs, n);
  oak_lexer_cleanup(lexer);
  return result;
}

OAK_TEST_MAIN(LexFloatExponent)
