#include "oak_lex.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_tok.h"

OAK_TEST_DECL(LexInteger)
{
  oak_lex_t lex;
  oak_lex_tokenize("1000", &lex);

  static oak_tok_attr_t attrs[] = {
    {
        .type = OAK_TOK_INT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .i_val = 1000,
    },
  };

  const oak_result_t result = oak_test_tokens(&lex, attrs, 1);
  oak_lex_cleanup(&lex);

  return result;
}

OAK_TEST_MAIN(LexInteger)
