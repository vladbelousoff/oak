#include "oak_lex.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_tok.h"

OAK_TEST_DECL(LexIdent)
{
  oak_lex_result_t* lex = oak_lex_tokenize("variable");

  static oak_tok_attr_t attrs[] = {
    {
        .type = OAK_TOK_IDENT,
        .line = 1,
        .column = 1,
        .pos = 1,
        .str = "variable",
    },
  };

  const oak_result_t result = oak_test_tokens(lex, attrs, 1);
  oak_lex_cleanup(lex);

  return result;
}

OAK_TEST_MAIN(LexIdent)
