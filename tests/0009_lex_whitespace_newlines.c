#include "oak_lex.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_tok.h"

OAK_TEST_DECL(LexWhitespaceAndNewlines)
{
  oak_lex_t lex;
  oak_lex_tokenize("1 \n 2\r\t3", &lex);

  static oak_tok_attr_t attrs[] = {
    {
        .type = OAK_TOK_INT_NUM,
        .line = 1,
        .column = 1,
        .pos = 1,
        .i_val = 1,
    },
    {
        .type = OAK_TOK_INT_NUM,
        .line = 2,
        .column = 2,
        .pos = 5,
        .i_val = 2,
    },
    {
        .type = OAK_TOK_INT_NUM,
        .line = 2,
        .column = 5,
        .pos = 8,
        .i_val = 3,
    },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(&lex, attrs, n);
  oak_lex_cleanup(&lex);
  return result;
}

OAK_TEST_MAIN(LexWhitespaceAndNewlines)
