#include "oak_count_of.h"
#include "oak_test_token.h"

#define LONG_A                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaaa"

OAK_TEST_DECL(LexNumbersIdentifiersAndOperators)
{
  oak_lexer_result_t* lexer =
      OAK_LEX("answer = 1 + 2.5 * 3e2 != 0;");

  static oak_expected_token_t expected[] = {
    { .kind = OAK_TOKEN_IDENT, .line = 1, .column = 1, .offset = 1, .string = "answer" },
    { .kind = OAK_TOKEN_ASSIGN, .line = 1, .column = 8, .offset = 8 },
    { .kind = OAK_TOKEN_INT, .line = 1, .column = 10, .offset = 10, .integer = 1 },
    { .kind = OAK_TOKEN_PLUS, .line = 1, .column = 12, .offset = 12 },
    { .kind = OAK_TOKEN_FLOAT, .line = 1, .column = 14, .offset = 14, .floating = 2.5f },
    { .kind = OAK_TOKEN_STAR, .line = 1, .column = 18, .offset = 18 },
    { .kind = OAK_TOKEN_FLOAT, .line = 1, .column = 20, .offset = 20, .floating = 300.0f },
    { .kind = OAK_TOKEN_BANG_EQUAL, .line = 1, .column = 24, .offset = 24 },
    { .kind = OAK_TOKEN_INT, .line = 1, .column = 27, .offset = 27, .integer = 0 },
    { .kind = OAK_TOKEN_SEMICOLON, .line = 1, .column = 28, .offset = 28 },
  };

  const oak_test_status_t result =
      oak_test_tokens(lexer, expected, oak_count_of(expected));
  oak_lexer_free(lexer);
  return result;
}

OAK_TEST_DECL(LexKeywordsAndPunctuation)
{
  oak_lexer_result_t* lexer = OAK_LEX(
      "let mut if else while for in break continue return true false "
      "fn record enum import export as from to new self weak interface");

  static oak_expected_token_t expected[] = {
    { .kind = OAK_TOKEN_LET, .line = 1, .column = 1, .offset = 1 },
    { .kind = OAK_TOKEN_MUT, .line = 1, .column = 5, .offset = 5 },
    { .kind = OAK_TOKEN_IF, .line = 1, .column = 9, .offset = 9 },
    { .kind = OAK_TOKEN_ELSE, .line = 1, .column = 12, .offset = 12 },
    { .kind = OAK_TOKEN_WHILE, .line = 1, .column = 17, .offset = 17 },
    { .kind = OAK_TOKEN_FOR, .line = 1, .column = 23, .offset = 23 },
    { .kind = OAK_TOKEN_IN, .line = 1, .column = 27, .offset = 27 },
    { .kind = OAK_TOKEN_BREAK, .line = 1, .column = 30, .offset = 30 },
    { .kind = OAK_TOKEN_CONTINUE, .line = 1, .column = 36, .offset = 36 },
    { .kind = OAK_TOKEN_RETURN, .line = 1, .column = 45, .offset = 45 },
    { .kind = OAK_TOKEN_TRUE, .line = 1, .column = 52, .offset = 52 },
    { .kind = OAK_TOKEN_FALSE, .line = 1, .column = 57, .offset = 57 },
    { .kind = OAK_TOKEN_FN, .line = 1, .column = 63, .offset = 63 },
    { .kind = OAK_TOKEN_RECORD, .line = 1, .column = 66, .offset = 66 },
    { .kind = OAK_TOKEN_ENUM, .line = 1, .column = 73, .offset = 73 },
    { .kind = OAK_TOKEN_IMPORT, .line = 1, .column = 78, .offset = 78 },
    { .kind = OAK_TOKEN_EXPORT, .line = 1, .column = 85, .offset = 85 },
    { .kind = OAK_TOKEN_AS, .line = 1, .column = 92, .offset = 92 },
    { .kind = OAK_TOKEN_FROM, .line = 1, .column = 95, .offset = 95 },
    { .kind = OAK_TOKEN_TO, .line = 1, .column = 100, .offset = 100 },
    { .kind = OAK_TOKEN_NEW, .line = 1, .column = 103, .offset = 103 },
    { .kind = OAK_TOKEN_SELF, .line = 1, .column = 107, .offset = 107 },
    { .kind = OAK_TOKEN_WEAK, .line = 1, .column = 112, .offset = 112 },
    { .kind = OAK_TOKEN_INTERFACE, .line = 1, .column = 117, .offset = 117 },
  };

  const oak_test_status_t result =
      oak_test_tokens(lexer, expected, oak_count_of(expected));
  oak_lexer_free(lexer);
  return result;
}

OAK_TEST_DECL(LexOperatorsAndPunctuation)
{
  oak_lexer_result_t* lexer =
      OAK_LEX("? == && || >= <= -= *= /= %= // : , ( ) { } [ ] > < . ->");

  static oak_expected_token_t expected[] = {
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_QUESTION, 1),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_EQUAL_EQUAL, 3),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_AND, 6),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_OR, 9),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_GREATER_EQUAL, 12),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_LESS_EQUAL, 15),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_MINUS_ASSIGN, 18),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_STAR_ASSIGN, 21),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_SLASH_ASSIGN, 24),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_PERCENT_ASSIGN, 27),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_DOUBLE_SLASH, 30),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_COLON, 33),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_COMMA, 35),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_LPAREN, 37),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_RPAREN, 39),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_LBRACE, 41),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_RBRACE, 43),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_LBRACKET, 45),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_RBRACKET, 47),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_GREATER, 49),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_LESS, 51),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_DOT, 53),
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_ARROW, 55),
  };

  const oak_test_status_t result =
      oak_test_tokens(lexer, expected, oak_count_of(expected));
  oak_lexer_free(lexer);
  return result;
}

OAK_TEST_DECL(LexTraitIsIdentifier)
{
  oak_lexer_result_t* lexer = OAK_LEX("trait");

  static oak_expected_token_t expected[] = {
    { .kind = OAK_TOKEN_IDENT,
      .line = 1,
      .column = 1,
      .offset = 1,
      .string = "trait" },
  };

  const oak_test_status_t result =
      oak_test_tokens(lexer, expected, oak_count_of(expected));
  oak_lexer_free(lexer);
  return result;
}

OAK_TEST_DECL(LexBlockComments)
{
  oak_lexer_result_t* lexer =
      OAK_LEX("let /* ignored\ncomment */ value = 1;");

  static oak_expected_token_t expected[] = {
    OAK_EXPECT_TOKEN_AT(OAK_TOKEN_LET, 1),
    { .kind = OAK_TOKEN_IDENT, .line = 2, .column = 12, .offset = 27, .string = "value" },
    { .kind = OAK_TOKEN_ASSIGN, .line = 2, .column = 18, .offset = 33 },
    { .kind = OAK_TOKEN_INT, .line = 2, .column = 20, .offset = 35, .integer = 1 },
    { .kind = OAK_TOKEN_SEMICOLON, .line = 2, .column = 21, .offset = 36 },
  };

  const oak_test_status_t result =
      oak_test_tokens(lexer, expected, oak_count_of(expected));
  oak_lexer_free(lexer);
  return result;
}

OAK_TEST_DECL(LexStringsEscapesUnicodeAndGrowth)
{
  oak_lexer_result_t* lexer =
      OAK_LEX("'hello' '\\n\\t\\r\\'\\\\' '\xCF\x80' '" LONG_A "'");

  static oak_expected_token_t expected[] = {
    { .kind = OAK_TOKEN_STRING, .line = 1, .column = 1, .offset = 1, .string = "hello" },
    { .kind = OAK_TOKEN_STRING, .line = 1, .column = 9, .offset = 9, .string = "\n\t\r'\\" },
    { .kind = OAK_TOKEN_STRING, .line = 1, .column = 22, .offset = 22, .string = "\xCF\x80" },
    { .kind = OAK_TOKEN_STRING, .line = 1, .column = 26, .offset = 26, .string = LONG_A },
  };

  const oak_test_status_t result =
      oak_test_tokens(lexer, expected, oak_count_of(expected));
  oak_lexer_free(lexer);
  return result;
}

OAK_TEST_DECL(LexWhitespaceNewlinesAndErrors)
{
  oak_lexer_result_t* lexer = OAK_LEX("let x = 1;\n\n  x += 2;");
  static oak_expected_token_t expected[] = {
    { .kind = OAK_TOKEN_LET, .line = 1, .column = 1, .offset = 1 },
    { .kind = OAK_TOKEN_IDENT, .line = 1, .column = 5, .offset = 5, .string = "x" },
    { .kind = OAK_TOKEN_ASSIGN, .line = 1, .column = 7, .offset = 7 },
    { .kind = OAK_TOKEN_INT, .line = 1, .column = 9, .offset = 9, .integer = 1 },
    { .kind = OAK_TOKEN_SEMICOLON, .line = 1, .column = 10, .offset = 10 },
    { .kind = OAK_TOKEN_IDENT, .line = 3, .column = 3, .offset = 15, .string = "x" },
    { .kind = OAK_TOKEN_PLUS_ASSIGN, .line = 3, .column = 5, .offset = 17 },
    { .kind = OAK_TOKEN_INT, .line = 3, .column = 8, .offset = 20, .integer = 2 },
    { .kind = OAK_TOKEN_SEMICOLON, .line = 3, .column = 9, .offset = 21 },
  };

  oak_test_status_t result =
      oak_test_tokens(lexer, expected, oak_count_of(expected));
  oak_lexer_free(lexer);
  OAK_CHECK(result == OAK_TEST_OK);

  lexer = OAK_LEX("'unterminated");
  OAK_CHECK(oak_lexer_error_count(lexer) > 0);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

/* Unrecognized characters are reported and skipped — lexing must terminate
 * (a regression here is an infinite loop, e.g. on double-quoted strings). */
OAK_TEST_DECL(LexErrorsTerminate)
{
  oak_lexer_result_t* lexer = OAK_LEX("let s = \"hi\";");
  OAK_CHECK(oak_lexer_error_count(lexer) == 2);
  oak_lexer_free(lexer);

  lexer = OAK_LEX("let x = 1 # 2;");
  OAK_CHECK(oak_lexer_error_count(lexer) == 1);
  oak_lexer_free(lexer);

  /* Unknown escape sequences are rejected. */
  lexer = OAK_LEX("'a\\q'");
  OAK_CHECK(oak_lexer_error_count(lexer) > 0);
  oak_lexer_free(lexer);

  /* Integer literals beyond the i32 range are rejected, not wrapped. */
  lexer = OAK_LEX("2147483648");
  OAK_CHECK(oak_lexer_error_count(lexer) > 0);
  oak_lexer_free(lexer);

  return OAK_TEST_OK;
}

int main(const int argc, char* argv[])
{
  (void)argc;
  (void)argv;
  static oak_test_t tests[] = {
    OAK_TEST_ENTRY(LexNumbersIdentifiersAndOperators),
    OAK_TEST_ENTRY(LexKeywordsAndPunctuation),
    OAK_TEST_ENTRY(LexOperatorsAndPunctuation),
    OAK_TEST_ENTRY(LexTraitIsIdentifier),
    OAK_TEST_ENTRY(LexBlockComments),
    OAK_TEST_ENTRY(LexStringsEscapesUnicodeAndGrowth),
    OAK_TEST_ENTRY(LexWhitespaceNewlinesAndErrors),
    OAK_TEST_ENTRY(LexErrorsTerminate),
  };
  return oak_test_run(tests, (int)oak_count_of(tests));
}
