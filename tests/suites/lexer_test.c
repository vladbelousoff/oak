/*
 * Lexer: token kinds, source positions, and literal values.
 *
 * Positions are asserted alongside kinds throughout. They are what every
 * diagnostic in the compiler and every breakpoint in the debugger is built
 * from, so a column that silently drifts is a real bug that a kind-only test
 * would never see.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(lexer);

/* 51 characters: longer than the lexer's initial literal buffer, so this also
 * covers the buffer growth path. */
#define LONG_IDENT                                                             \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "aaaaaaaaaa"                                                                 \
  "a"

UTEST_F(lexer, numbers_identifiers_and_operators)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("answer = 1 + 2.5 * 3e2 != 0;", OAK_A);

  static const oak_expect_token_t expected[] = {
    { OAK_TOKEN_IDENT, 1, 1, 1, "answer", 0, 0.0f },
    OAK_TOKEN_AT(OAK_TOKEN_ASSIGN, 8),
    { OAK_TOKEN_INT, 1, 10, 10, null, 1, 0.0f },
    OAK_TOKEN_AT(OAK_TOKEN_PLUS, 12),
    { OAK_TOKEN_FLOAT, 1, 14, 14, null, 0, 2.5f },
    OAK_TOKEN_AT(OAK_TOKEN_STAR, 18),
    { OAK_TOKEN_FLOAT, 1, 20, 20, null, 0, 300.0f },
    OAK_TOKEN_AT(OAK_TOKEN_BANG_EQUAL, 24),
    { OAK_TOKEN_INT, 1, 27, 27, null, 0, 0.0f },
    OAK_TOKEN_AT(OAK_TOKEN_SEMICOLON, 28),
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  oak_lexer_free(lexer);
}

UTEST_F(lexer, keywords)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "let mut if else while for in break continue return true false "
      "fn record enum import export as from to new self weak interface "
      "implements static",
      OAK_A);

  static const oak_expect_token_t expected[] = {
    OAK_TOKEN_AT(OAK_TOKEN_LET, 1),
    OAK_TOKEN_AT(OAK_TOKEN_MUT, 5),
    OAK_TOKEN_AT(OAK_TOKEN_IF, 9),
    OAK_TOKEN_AT(OAK_TOKEN_ELSE, 12),
    OAK_TOKEN_AT(OAK_TOKEN_WHILE, 17),
    OAK_TOKEN_AT(OAK_TOKEN_FOR, 23),
    OAK_TOKEN_AT(OAK_TOKEN_IN, 27),
    OAK_TOKEN_AT(OAK_TOKEN_BREAK, 30),
    OAK_TOKEN_AT(OAK_TOKEN_CONTINUE, 36),
    OAK_TOKEN_AT(OAK_TOKEN_RETURN, 45),
    OAK_TOKEN_AT(OAK_TOKEN_TRUE, 52),
    OAK_TOKEN_AT(OAK_TOKEN_FALSE, 57),
    OAK_TOKEN_AT(OAK_TOKEN_FN, 63),
    OAK_TOKEN_AT(OAK_TOKEN_RECORD, 66),
    OAK_TOKEN_AT(OAK_TOKEN_ENUM, 73),
    OAK_TOKEN_AT(OAK_TOKEN_IMPORT, 78),
    OAK_TOKEN_AT(OAK_TOKEN_EXPORT, 85),
    OAK_TOKEN_AT(OAK_TOKEN_AS, 92),
    OAK_TOKEN_AT(OAK_TOKEN_FROM, 95),
    OAK_TOKEN_AT(OAK_TOKEN_TO, 100),
    OAK_TOKEN_AT(OAK_TOKEN_NEW, 103),
    OAK_TOKEN_AT(OAK_TOKEN_SELF, 107),
    OAK_TOKEN_AT(OAK_TOKEN_WEAK, 112),
    OAK_TOKEN_AT(OAK_TOKEN_INTERFACE, 117),
    OAK_TOKEN_AT(OAK_TOKEN_IMPLEMENTS, 127),
    OAK_TOKEN_AT(OAK_TOKEN_STATIC, 138),
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  oak_lexer_free(lexer);
}

UTEST_F(lexer, operators_and_punctuation)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "? == && || >= <= -= *= /= %= // : , ( ) { } [ ] > < . ->", OAK_A);

  static const oak_expect_token_t expected[] = {
    OAK_TOKEN_AT(OAK_TOKEN_QUESTION, 1),
    OAK_TOKEN_AT(OAK_TOKEN_EQUAL_EQUAL, 3),
    OAK_TOKEN_AT(OAK_TOKEN_AND, 6),
    OAK_TOKEN_AT(OAK_TOKEN_OR, 9),
    OAK_TOKEN_AT(OAK_TOKEN_GREATER_EQUAL, 12),
    OAK_TOKEN_AT(OAK_TOKEN_LESS_EQUAL, 15),
    OAK_TOKEN_AT(OAK_TOKEN_MINUS_ASSIGN, 18),
    OAK_TOKEN_AT(OAK_TOKEN_STAR_ASSIGN, 21),
    OAK_TOKEN_AT(OAK_TOKEN_SLASH_ASSIGN, 24),
    OAK_TOKEN_AT(OAK_TOKEN_PERCENT_ASSIGN, 27),
    OAK_TOKEN_AT(OAK_TOKEN_DOUBLE_SLASH, 30),
    OAK_TOKEN_AT(OAK_TOKEN_COLON, 33),
    OAK_TOKEN_AT(OAK_TOKEN_COMMA, 35),
    OAK_TOKEN_AT(OAK_TOKEN_LPAREN, 37),
    OAK_TOKEN_AT(OAK_TOKEN_RPAREN, 39),
    OAK_TOKEN_AT(OAK_TOKEN_LBRACE, 41),
    OAK_TOKEN_AT(OAK_TOKEN_RBRACE, 43),
    OAK_TOKEN_AT(OAK_TOKEN_LBRACKET, 45),
    OAK_TOKEN_AT(OAK_TOKEN_RBRACKET, 47),
    OAK_TOKEN_AT(OAK_TOKEN_GREATER, 49),
    OAK_TOKEN_AT(OAK_TOKEN_LESS, 51),
    OAK_TOKEN_AT(OAK_TOKEN_DOT, 53),
    OAK_TOKEN_AT(OAK_TOKEN_ARROW, 55),
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  oak_lexer_free(lexer);
}

/* `trait` is deliberately not a keyword; interfaces use `interface`. */
UTEST_F(lexer, trait_is_an_identifier)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("trait", OAK_A);
  static const oak_expect_token_t expected[] = {
    { OAK_TOKEN_IDENT, 1, 1, 1, "trait", 0, 0.0f },
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  oak_lexer_free(lexer);
}

/* A block comment spanning lines must still advance the line counter. */
UTEST_F(lexer, block_comments_span_lines)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("let /* ignored\ncomment */ value = 1;", OAK_A);

  static const oak_expect_token_t expected[] = {
    OAK_TOKEN_AT(OAK_TOKEN_LET, 1),
    { OAK_TOKEN_IDENT, 2, 12, 27, "value", 0, 0.0f },
    { OAK_TOKEN_ASSIGN, 2, 18, 33, null, 0, 0.0f },
    { OAK_TOKEN_INT, 2, 20, 35, null, 1, 0.0f },
    { OAK_TOKEN_SEMICOLON, 2, 21, 36, null, 0, 0.0f },
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  oak_lexer_free(lexer);
}

/*
 * Oak has block comments only. `//` is the integer-division operator, so a
 * C-style line comment silently parses as arithmetic rather than being
 * ignored -- worth pinning down, since it is the obvious thing to get wrong.
 */
UTEST_F(lexer, double_slash_is_integer_division_not_a_comment)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize("7 // 2", OAK_A);

  static const oak_expect_token_t expected[] = {
    { OAK_TOKEN_INT, 1, 1, 1, null, 7, 0.0f },
    OAK_TOKEN_AT(OAK_TOKEN_DOUBLE_SLASH, 3),
    { OAK_TOKEN_INT, 1, 6, 6, null, 2, 0.0f },
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  EXPECT_EQ(0, oak_lexer_error_count(lexer));
  oak_lexer_free(lexer);
}

UTEST_F(lexer, strings_escapes_and_utf8)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "'hello' '\\n\\t\\r\\'\\\\' '\xCF\x80' '" LONG_IDENT "'", OAK_A);

  static const oak_expect_token_t expected[] = {
    { OAK_TOKEN_STRING, 1, 1, 1, "hello", 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 9, 9, "\n\t\r'\\", 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 22, 22, "\xCF\x80", 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 26, 26, LONG_IDENT, 0, 0.0f },
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  oak_lexer_free(lexer);
}

UTEST_F(lexer, newlines_advance_line_and_offset)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("let x = 1;\n\n  x += 2;", OAK_A);

  static const oak_expect_token_t expected[] = {
    OAK_TOKEN_AT(OAK_TOKEN_LET, 1),
    { OAK_TOKEN_IDENT, 1, 5, 5, "x", 0, 0.0f },
    OAK_TOKEN_AT(OAK_TOKEN_ASSIGN, 7),
    { OAK_TOKEN_INT, 1, 9, 9, null, 1, 0.0f },
    OAK_TOKEN_AT(OAK_TOKEN_SEMICOLON, 10),
    { OAK_TOKEN_IDENT, 3, 3, 15, "x", 0, 0.0f },
    { OAK_TOKEN_PLUS_ASSIGN, 3, 5, 17, null, 0, 0.0f },
    { OAK_TOKEN_INT, 3, 8, 20, null, 2, 0.0f },
    { OAK_TOKEN_SEMICOLON, 3, 9, 21, null, 0, 0.0f },
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  oak_lexer_free(lexer);
}

/* A CRLF file must not count the carriage return as its own line, or every
 * diagnostic in a Windows-authored source is off by one. */
UTEST_F(lexer, crlf_counts_one_line_per_pair)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("let x = 1;\r\nlet y = 2;", OAK_A);
  usize index = 0;
  oak_list_entry_t* entry;
  int last_line = 0;

  oak_list_for_each_indexed(index, entry, oak_lexer_tokens(lexer))
  {
    const oak_token_t* tok = oak_container_of(entry, oak_token_t, link);
    last_line = oak_token_line(tok);
  }

  EXPECT_EQ(0, oak_lexer_error_count(lexer));
  EXPECT_EQ(2, last_line);
  oak_lexer_free(lexer);
}

/*
 * Malformed input must be reported and lexing must terminate. A regression
 * here is an infinite loop rather than a wrong answer -- double-quoted strings
 * caused exactly that once.
 */
UTEST_F(lexer, malformed_input_is_reported_and_terminates)
{
  static const struct
  {
    const char* src;
    int min_errors;
  } cases[] = {
    { "let s = \"hi\";", 2 },  /* double quotes are not string syntax */
    { "let x = 1 # 2;", 1 },   /* unknown character */
    { "'unterminated", 1 },    /* string with no closing quote */
    { "'a\\q'", 1 },           /* unknown escape */
    { "2147483648", 1 },       /* integer past the i32 range, not wrapped */
    { "/* never closed", 1 },  /* block comment with no terminator */
  };

  usize i;
  for (i = 0; i < OAK_COUNT_OF(cases); ++i)
  {
    oak_lexer_result_t* lexer = oak_lexer_tokenize(cases[i].src, OAK_A);
    const int errors = oak_lexer_error_count(lexer);
    if (errors < cases[i].min_errors)
    {
      UTEST_PRINTF("  '%s': %d errors, want at least %d\n",
                   cases[i].src,
                   errors,
                   cases[i].min_errors);
      *utest_result = UTEST_TEST_FAILURE;
    }
    oak_lexer_free(lexer);
  }
}
