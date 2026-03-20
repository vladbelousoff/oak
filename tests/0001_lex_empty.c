#include "oak_common.h"
#include "oak_lex.h"
#include "oak_test.h"
#include "oak_test_run.h"

OAK_TEST_DECL(LexEmptyString)
{
  oak_lex_result_t* lex = oak_lex_tokenize("");
  const oak_result_t result =
      oak_list_empty(oak_lex_tokens(lex)) ? OAK_SUCCESS : OAK_FAILURE;
  oak_lex_cleanup(lex);
  return result;
}

OAK_TEST_MAIN(LexEmptyString)
