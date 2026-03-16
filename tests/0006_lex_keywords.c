#include "oak_lex.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_tok.h"

OAK_TEST_DECL(LexKeywords)
{
  oak_lex_t lex;
  oak_lex_tokenize(
      "let mut if else while for break continue return true false and or not",
      &lex);

  static oak_tok_attr_t attrs[] = {
    {
        .type = OAK_TOK_LET,
        .line = 1,
        .column = 1,
        .pos = 1,
    },
    {
        .type = OAK_TOK_MUT,
        .line = 1,
        .column = 5,
        .pos = 5,
    },
    {
        .type = OAK_TOK_IF,
        .line = 1,
        .column = 9,
        .pos = 9,
    },
    {
        .type = OAK_TOK_ELSE,
        .line = 1,
        .column = 12,
        .pos = 12,
    },
    {
        .type = OAK_TOK_WHILE,
        .line = 1,
        .column = 17,
        .pos = 17,
    },
    {
        .type = OAK_TOK_FOR,
        .line = 1,
        .column = 23,
        .pos = 23,
    },
    {
        .type = OAK_TOK_BREAK,
        .line = 1,
        .column = 27,
        .pos = 27,
    },
    {
        .type = OAK_TOK_CONTINUE,
        .line = 1,
        .column = 33,
        .pos = 33,
    },
    {
        .type = OAK_TOK_RETURN,
        .line = 1,
        .column = 42,
        .pos = 42,
    },
    {
        .type = OAK_TOK_TRUE,
        .line = 1,
        .column = 49,
        .pos = 49,
    },
    {
        .type = OAK_TOK_FALSE,
        .line = 1,
        .column = 54,
        .pos = 54,
    },
    {
        .type = OAK_TOK_AND,
        .line = 1,
        .column = 60,
        .pos = 60,
    },
    {
        .type = OAK_TOK_OR,
        .line = 1,
        .column = 64,
        .pos = 64,
    },
    {
        .type = OAK_TOK_NOT,
        .line = 1,
        .column = 67,
        .pos = 67,
    },
  };

  const size_t n = sizeof(attrs) / sizeof(attrs[0]);
  const oak_result_t result = oak_test_tokens(&lex, attrs, n);
  oak_lex_cleanup(&lex);

  return result;
}

OAK_TEST_MAIN(LexKeywords)
