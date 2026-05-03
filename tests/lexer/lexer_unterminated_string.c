#include "oak_test_token.h"

/* A string literal missing its closing quote produces a lexer error. */
OAK_TEST_DECL(LexUnterminatedString)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("'hello world");
  OAK_CHECK(oak_lexer_error_count(lexer) > 0);
  oak_lexer_free(lexer);
  return OAK_TEST_OK;
}

OAK_TEST_MAIN(LexUnterminatedString)
