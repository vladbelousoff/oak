#include "oak_lexer.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_token.h"

OAK_TEST_DECL(LexKeywords)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "let mut if else while for break continue return true false and or not");

  static oak_token_attr_t attrs[] = {
    {
        .type = OAK_TOKEN_LET,
        .line = 1,
        .column = 1,
        .pos = 1,
    },
    {
        .type = OAK_TOKEN_MUT,
        .line = 1,
        .column = 5,
        .pos = 5,
    },
    {
        .type = OAK_TOKEN_IF,
        .line = 1,
        .column = 9,
        .pos = 9,
    },
    {
        .type = OAK_TOKEN_ELSE,
        .line = 1,
        .column = 12,
        .pos = 12,
    },
    {
        .type = OAK_TOKEN_WHILE,
        .line = 1,
        .column = 17,
        .pos = 17,
    },
    {
        .type = OAK_TOKEN_FOR,
        .line = 1,
        .column = 23,
        .pos = 23,
    },
    {
        .type = OAK_TOKEN_BREAK,
        .line = 1,
        .column = 27,
        .pos = 27,
    },
    {
        .type = OAK_TOKEN_CONTINUE,
        .line = 1,
        .column = 33,
        .pos = 33,
    },
    {
        .type = OAK_TOKEN_RETURN,
        .line = 1,
        .column = 42,
        .pos = 42,
    },
    {
        .type = OAK_TOKEN_TRUE,
        .line = 1,
        .column = 49,
        .pos = 49,
    },
    {
        .type = OAK_TOKEN_FALSE,
        .line = 1,
        .column = 54,
        .pos = 54,
    },
    {
        .type = OAK_TOKEN_AND,
        .line = 1,
        .column = 60,
        .pos = 60,
    },
    {
        .type = OAK_TOKEN_OR,
        .line = 1,
        .column = 64,
        .pos = 64,
    },
    {
        .type = OAK_TOKEN_NOT,
        .line = 1,
        .column = 67,
        .pos = 67,
    },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lexer, attrs, n);
  oak_lexer_cleanup(lexer);

  return result;
}

OAK_TEST_MAIN(LexKeywords)
