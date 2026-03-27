#include "oak_common.h"
#include "oak_lexer.h"
#include "oak_test.h"
#include "oak_test_run.h"

OAK_TEST_DECL(LexEmptyString)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("");
  const oak_result_t result =
      oak_list_empty(oak_lexer_tokens(lexer)) ? OAK_SUCCESS : OAK_FAILURE;
  oak_lexer_cleanup(lexer);
  return result;
}

OAK_TEST_MAIN(LexEmptyString)
