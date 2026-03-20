#include "oak_lexer.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_token.h"

OAK_TEST_DECL(LexInteger)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("1000");

  static oak_token_attr_t attrs[] = {
    {
        .type = OAK_TOKEN_INT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .i_val = 1000,
    },
  };

  const oak_result_t result = oak_test_tokens(lexer, attrs, 1);
  oak_lexer_cleanup(lexer);

  return result;
}

OAK_TEST_MAIN(LexInteger)
