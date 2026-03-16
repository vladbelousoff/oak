#include "oak_lex.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_tok.h"

OAK_TEST_DECL(LexFloat)
{
  oak_lex_t lex;
  oak_lex_tokenize("77.23", &lex);

  static oak_tok_attr_t attrs[] = {
    {
        .type = OAK_TOK_FLOAT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .f_val = 77.23f,
    },
  };

  const oak_result_t result = oak_test_tokens(&lex, attrs, 1);
  oak_lex_cleanup(&lex);

  return result;
}

OAK_TEST_MAIN(LexFloat)
