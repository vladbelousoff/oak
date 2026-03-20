#include "oak_lex.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_tok.h"

OAK_TEST_DECL(LexArithmeticExpression)
{
  oak_lex_result_t* lex = oak_lex_tokenize("1 + 2 * 3");

  static oak_tok_attr_t attrs[] = {
    {
        .type = OAK_TOK_INT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .i_val = 1,
    },
    {
        .type = OAK_TOK_PLUS,
        .line = 1,
        .column = 3,
        .pos = 3,
    },
    {
        .type = OAK_TOK_INT_NUM,
        .line = 1,
        .column = 5,
        .pos = 5,
        .i_val = 2,
    },
    {
        .type = OAK_TOK_STAR,
        .line = 1,
        .column = 7,
        .pos = 7,
    },
    {
        .type = OAK_TOK_INT_NUM,
        .line = 1,
        .column = 9,
        .pos = 9,
        .i_val = 3,
    },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lex, attrs, n);
  oak_lex_cleanup(lex);

  return result;
}

OAK_TEST_MAIN(LexArithmeticExpression)
