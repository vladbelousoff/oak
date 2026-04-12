#include "oak_lexer.h"
#include "oak_test_run.h"

OAK_TEST_DECL(LexEmptyString)
{
  struct oak_lexer_result_t* lexer = oak_lexer_tokenize("");
  OAK_CHECK(oak_list_empty(oak_lexer_tokens(lexer)));
  oak_lexer_cleanup(lexer);
  return OAK_SUCCESS;
}

OAK_TEST_MAIN(LexEmptyString)
