#include "oak_test_token.h"

OAK_TEST_DECL(LexIdent)
{
  struct oak_lexer_result_t* lexer = OAK_LEX("variable");

  static struct oak_expected_token_t expected_tokens[] = {
    {
        .kind = OAK_TOKEN_IDENT,
        .line = 1,
        .column = 1,
        .pos = 1,
        .string = "variable",
    },
  };

  const enum oak_test_status_t result =
      oak_test_tokens(lexer, expected_tokens, 1);
  oak_lexer_cleanup(lexer);

  return result;
}

OAK_TEST_MAIN(LexIdent)
