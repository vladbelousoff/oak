#include "oak_test_token.h"

#define LONG_A                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaaa"

OAK_TEST_DECL(LexString)
{
  /* Includes escapes + UTF-8 bytes (π) + a long string to hit a dynamic buffer.
   */
  struct oak_lexer_result_t* lexer =
      oak_lexer_tokenize("'hello' '\\n\\t\\r\\'\\\\' '\xCF\x80' "
                         "'" LONG_A "'");

  static struct oak_expected_token_t expected_tokens[] = {
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 1,
        .pos = 1,
        .string = "hello",
    },
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 9,
        .pos = 9,
        .string = "\n\t\r'\\",
    },
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 22,
        .pos = 22,
        .string = "\xCF\x80",
    },
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 26,
        .pos = 26,
        .string = LONG_A,
    },
  };

  const size_t n = oak_countof(expected_tokens);
  const enum oak_test_status_t result =
      oak_test_tokens(lexer, expected_tokens, n);
  oak_lexer_cleanup(lexer);
  return result;
}

OAK_TEST_MAIN(LexString)
