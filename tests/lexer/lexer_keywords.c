#include "oak_test_token.h"

OAK_TEST_DECL(LexKeywords)
{
  struct oak_lexer_result_t* lexer = OAK_LEX(
      "let mut if else while for break continue return true false and or not");

  static struct oak_expected_token_t expected_tokens[] = {
    {
        .kind = OAK_TOKEN_LET,
        .line = 1,
        .column = 1,
        .offset = 1,
    },
    {
        .kind = OAK_TOKEN_MUT,
        .line = 1,
        .column = 5,
        .offset = 5,
    },
    {
        .kind = OAK_TOKEN_IF,
        .line = 1,
        .column = 9,
        .offset = 9,
    },
    {
        .kind = OAK_TOKEN_ELSE,
        .line = 1,
        .column = 12,
        .offset = 12,
    },
    {
        .kind = OAK_TOKEN_WHILE,
        .line = 1,
        .column = 17,
        .offset = 17,
    },
    {
        .kind = OAK_TOKEN_FOR,
        .line = 1,
        .column = 23,
        .offset = 23,
    },
    {
        .kind = OAK_TOKEN_BREAK,
        .line = 1,
        .column = 27,
        .offset = 27,
    },
    {
        .kind = OAK_TOKEN_CONTINUE,
        .line = 1,
        .column = 33,
        .offset = 33,
    },
    {
        .kind = OAK_TOKEN_RETURN,
        .line = 1,
        .column = 42,
        .offset = 42,
    },
    {
        .kind = OAK_TOKEN_TRUE,
        .line = 1,
        .column = 49,
        .offset = 49,
    },
    {
        .kind = OAK_TOKEN_FALSE,
        .line = 1,
        .column = 54,
        .offset = 54,
    },
    {
        .kind = OAK_TOKEN_AND,
        .line = 1,
        .column = 60,
        .offset = 60,
    },
    {
        .kind = OAK_TOKEN_OR,
        .line = 1,
        .column = 64,
        .offset = 64,
    },
    {
        .kind = OAK_TOKEN_NOT,
        .line = 1,
        .column = 67,
        .offset = 67,
    },
  };

  const usize n = oak_count_of(expected_tokens);
  const enum oak_test_status_t result =
      oak_test_tokens(lexer, expected_tokens, n);
  oak_lexer_free(lexer);

  return result;
}

OAK_TEST_MAIN(LexKeywords)
