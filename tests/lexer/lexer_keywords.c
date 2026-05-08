#include "oak_test_token.h"

OAK_TEST_DECL(LexKeywords)
{
  struct oak_lexer_result_t* lexer = OAK_LEX(
      "and as break continue else enum false fn for from if import in let mut "
      "new not or return self record to true while");

  static struct oak_expected_token_t expected_tokens[] = {
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_AND, 1),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_AS, 5),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_BREAK, 8),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_CONTINUE, 14),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_ELSE, 23),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_ENUM, 28),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_FALSE, 33),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_FN, 39),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_FOR, 42),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_FROM, 46),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_IF, 51),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_IMPORT, 54),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_IN, 61),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_LET, 64),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_MUT, 68),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_NEW, 72),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_NOT, 76),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_OR, 80),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_RETURN, 83),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_SELF, 90),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_RECORD, 95),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_TO, 102),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_TRUE, 105),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_WHILE, 110),
  };

  const usize n = oak_count_of(expected_tokens);
  const enum oak_test_status_t result =
      oak_test_tokens(lexer, expected_tokens, n);
  oak_lexer_free(lexer);

  return result;
}

OAK_TEST_MAIN(LexKeywords)
