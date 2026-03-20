#include "oak_lex.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_tok.h"

OAK_TEST_DECL(LexFloatExponent)
{
  oak_lex_result_t* lex = oak_lex_tokenize("1e2 3.0E1 0.5e3");

  static oak_tok_attr_t attrs[] = {
    {
        .type = OAK_TOK_FLOAT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .f_val = 100.0f,
    },
    {
        .type = OAK_TOK_FLOAT_NUM,
        .line = 1,
        .column = 5,
        .pos = 5,
        .f_val = 30.0f,
    },
    {
        .type = OAK_TOK_FLOAT_NUM,
        .line = 1,
        .column = 11,
        .pos = 11,
        .f_val = 500.0f,
    },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lex, attrs, n);
  oak_lex_cleanup(lex);
  return result;
}

OAK_TEST_MAIN(LexFloatExponent)
