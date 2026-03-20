#include "oak_lexer.h"
#include "oak_test.h"
#include "oak_test_run.h"
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
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("'hello' '\\n\\t\\r\\'\\\\' '\xCF\x80' "
                         "'" LONG_A "'");

  static oak_token_attr_t attrs[] = {
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 1,
        .pos = 1,
        .str = "hello",
    },
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 9,
        .pos = 9,
        .str = "\n\t\r'\\",
    },
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 22,
        .pos = 22,
        .str = "\xCF\x80",
    },
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 26,
        .pos = 26,
        .str = LONG_A,
    },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lexer, attrs, n);
  oak_lexer_cleanup(lexer);
  return result;
}

OAK_TEST_MAIN(LexString)
