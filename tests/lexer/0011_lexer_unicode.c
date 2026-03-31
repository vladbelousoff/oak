#include "oak_test_token.h"

OAK_TEST_DECL(LexUnicode)
{
  /* café 名 = '世界'
   *
   * café   → IDENT  col=1  pos=1  (2-byte é in identifier)
   * 名     → IDENT  col=6  pos=6  (3-byte CJK identifier)
   * =      → ASSIGN col=8  pos=8
   * '世界' → STRING col=10 pos=10 (3-byte CJK chars in string)
   */
  oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "caf\xC3\xA9 \xE5\x90\x8D = '\xE4\xB8\x96\xE7\x95\x8C'");

  static oak_token_attr_t attrs[] = {
    {
        .kind = OAK_TOKEN_IDENT,
        .line = 1,
        .column = 1,
        .pos = 1,
        .str = "caf\xC3\xA9",
    },
    {
        .kind = OAK_TOKEN_IDENT,
        .line = 1,
        .column = 6,
        .pos = 6,
        .str = "\xE5\x90\x8D",
    },
    {
        .kind = OAK_TOKEN_ASSIGN,
        .line = 1,
        .column = 8,
        .pos = 8,
    },
    {
        .kind = OAK_TOKEN_STRING,
        .line = 1,
        .column = 10,
        .pos = 10,
        .str = "\xE4\xB8\x96\xE7\x95\x8C",
    },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lexer, attrs, n);
  oak_lexer_cleanup(lexer);
  return result;
}

OAK_TEST_MAIN(LexUnicode)
