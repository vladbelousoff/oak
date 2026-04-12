#include "oak_test_token.h"

OAK_TEST_DECL(LexEmptyString)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("");
  OAK_CHECK(oak_list_empty(oak_lexer_tokens(lexer)));
  oak_lexer_cleanup(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_MAIN(LexEmptyString)
