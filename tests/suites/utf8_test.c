/*
 * UTF-8: source text, string literals, and the string methods that index into
 * them.
 *
 * Oak source is UTF-8 and so are its strings. Two properties are worth pinning
 * down separately. The lexer must accept multibyte text -- in identifiers, in
 * literals, and via \u escapes -- while rejecting byte sequences that are not
 * valid UTF-8, so an encoding mistake surfaces at compile time rather than as
 * mojibake later. The runtime must then index that text by character, so
 * substring() can never cut a character in half and size() always agrees with
 * the positions substring() and index_of() speak in.
 *
 * Non-ASCII text is written as explicit hex escapes rather than literal bytes.
 * That keeps the file's own encoding out of the picture -- these tests assert
 * on exact byte sequences, so the bytes should be visible at the call site --
 * and it survives a compiler that would otherwise read the source as some
 * locale codepage. String-literal concatenation separates an escape from any
 * following character that C would swallow as another hex digit.
 */

#include "oak_test_support.h"

OAK_TEST_SUITE(utf8);

#define U_EACUTE "\xC3\xA9"         /* U+00E9  e-acute, 2 bytes */
#define U_PI "\xCF\x80"             /* U+03C0  greek small letter pi, 2 bytes */
#define U_SNOWMAN "\xE2\x98\x83"    /* U+2603  snowman, 3 bytes */
#define U_GRIN "\xF0\x9F\x98\x80"   /* U+1F600 grinning face, 4 bytes */

/* "h<e-acute>llo": 5 characters, 6 bytes. The workhorse of the runtime tests
 * below, because the character and byte lengths differ by exactly one and the
 * multibyte character sits in the interior rather than at either end. */
#define HELLO "h" U_EACUTE "llo"

/* ---------------------------------------------------------------- lexer -- */

/* Multibyte characters in a literal reach the token unchanged, whatever plane
 * they come from. A 4-byte character matters on its own: it is the case a
 * decoder written for the BMP gets wrong. */
UTEST_F(utf8, string_literals_carry_multibyte_text_through)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("'" HELLO "' '" U_SNOWMAN "' '" U_GRIN "'", OAK_A);

  static const oak_expect_token_t expected[] = {
    /* Each literal is quotes plus its character count wide, whatever it
     * weighs in bytes: 7 columns, then 3, then 3. */
    { OAK_TOKEN_STRING, 1, 1, 1, HELLO, 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 9, 9, U_SNOWMAN, 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 13, 13, U_GRIN, 0, 0.0f },
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  EXPECT_EQ(0, oak_lexer_error_count(lexer));
  oak_lexer_free(lexer);
}

/* Columns advance per character, not per byte. Asserted because every
 * diagnostic and every debugger breakpoint is built from these numbers: the
 * third token's column is only correct if the 6-byte, 5-character literal
 * before it counted as 5. */
UTEST_F(utf8, columns_count_characters_not_bytes)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("let s = '" HELLO "';", OAK_A);

  static const oak_expect_token_t expected[] = {
    OAK_TOKEN_AT(OAK_TOKEN_LET, 1),
    { OAK_TOKEN_IDENT, 1, 5, 5, "s", 0, 0.0f },
    OAK_TOKEN_AT(OAK_TOKEN_ASSIGN, 7),
    { OAK_TOKEN_STRING, 1, 9, 9, HELLO, 0, 0.0f },
    /* 'h<e-acute>llo' spans columns 9..15, so the semicolon is at 16. */
    OAK_TOKEN_AT(OAK_TOKEN_SEMICOLON, 16),
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  EXPECT_EQ(0, oak_lexer_error_count(lexer));
  oak_lexer_free(lexer);
}

/* Identifiers may be non-ASCII, and a multibyte character is a letter for the
 * purpose of starting one. */
UTEST_F(utf8, identifiers_may_be_non_ascii)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("caf" U_EACUTE " = " U_PI "2;", OAK_A);

  static const oak_expect_token_t expected[] = {
    { OAK_TOKEN_IDENT, 1, 1, 1, "caf" U_EACUTE, 0, 0.0f },
    OAK_TOKEN_AT(OAK_TOKEN_ASSIGN, 6),
    /* A digit is allowed after the first character but not as it. */
    { OAK_TOKEN_IDENT, 1, 8, 8, U_PI "2", 0, 0.0f },
    OAK_TOKEN_AT(OAK_TOKEN_SEMICOLON, 10),
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  EXPECT_EQ(0, oak_lexer_error_count(lexer));
  oak_lexer_free(lexer);
}

/* \u{H...H} names a codepoint by number, so text that is awkward to type (or
 * to keep intact through a copy-paste) can still be written. The braces make
 * it width-independent: a character outside the BMP needs no surrogate pair. */
UTEST_F(utf8, unicode_escapes_encode_their_codepoint)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize(
      "'\\u{41}' '\\u{e9}' '\\u{03C0}' '\\u{2603}' '\\u{1F600}'", OAK_A);

  static const oak_expect_token_t expected[] = {
    /* ASCII range, lowercase and uppercase hex, and all three multibyte
     * widths. */
    { OAK_TOKEN_STRING, 1, 1, 1, "A", 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 10, 10, U_EACUTE, 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 19, 19, U_PI, 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 30, 30, U_SNOWMAN, 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 41, 41, U_GRIN, 0, 0.0f },
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  EXPECT_EQ(0, oak_lexer_error_count(lexer));
  oak_lexer_free(lexer);
}

/* An escape and the literal character it names are the same string, and an
 * escape composes with ordinary text around it. */
UTEST_F(utf8, unicode_escapes_and_literal_characters_agree)
{
  oak_lexer_result_t* lexer =
      oak_lexer_tokenize("'h\\u{e9}llo' '" HELLO "'", OAK_A);

  static const oak_expect_token_t expected[] = {
    { OAK_TOKEN_STRING, 1, 1, 1, HELLO, 0, 0.0f },
    { OAK_TOKEN_STRING, 1, 14, 14, HELLO, 0, 0.0f },
  };

  OAK_EXPECT_TOKENS(lexer, expected);
  EXPECT_EQ(0, oak_lexer_error_count(lexer));
  oak_lexer_free(lexer);
}

/*
 * Lexes src and hands back both the error count and whatever the lexer wrote
 * to stderr. Lexer diagnostics go through OAK_LOG rather than the compiler's
 * diagnostic buffer, so redirecting the stream is the only way to read one
 * back -- and the message is worth asserting, because a count alone passes
 * just as happily when the source is rejected for an unrelated reason.
 */
static int lex_capturing_stderr(oak_allocator_t* a,
                                const char* src,
                                char* out,
                                const usize cap)
{
  oak_capture_t capture;
  oak_capture_begin(&capture, stderr);
  oak_lexer_result_t* lexer = oak_lexer_tokenize(src, a);
  const int errors = oak_lexer_error_count(lexer);
  oak_lexer_free(lexer);
  oak_capture_end(&capture, out, cap);
  return errors;
}

/* Every row must be rejected by the lexer, with a message containing `want`. */
#define OAK_EXPECT_LEX_ERROR_CASES(tbl)                                        \
  do                                                                           \
  {                                                                            \
    for (usize oak_i = 0; oak_i < OAK_COUNT_OF(tbl); ++oak_i)                  \
    {                                                                          \
      char oak_msg[OAK_TEST_OUTPUT_MAX];                                       \
      const int oak_errors = lex_capturing_stderr(                             \
          OAK_A, (tbl)[oak_i].src, oak_msg, sizeof(oak_msg));                  \
      if (oak_errors == 0)                                                     \
      {                                                                        \
        UTEST_PRINTF("  expected a lex error, row %u\n", (unsigned)oak_i);     \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
      else if (!oak_test_contains(oak_msg, (tbl)[oak_i].want))                 \
      {                                                                        \
        UTEST_PRINTF("  wrong message, row %u: want substring '%s'\n"          \
                     "    got: %s\n",                                          \
                     (unsigned)oak_i,                                          \
                     (tbl)[oak_i].want,                                        \
                     oak_msg);                                                 \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
    }                                                                          \
  } while (0)

/* One row per way a \u escape can be malformed. Each must be an error rather
 * than a silently truncated or wrapped codepoint. The message separates the
 * two mistakes: syntax the lexer could not read, versus syntax it read fine
 * that named a value UTF-8 has no encoding for. */
UTEST_F(utf8, malformed_unicode_escapes_are_errors)
{
  static const oak_case_t cases[] = {
    /* Unreadable syntax. */
    { "'\\u'", "unknown escape sequence" },          /* no body at all */
    { "'\\u41'", "unknown escape sequence" },        /* braces are required */
    { "'\\u{}'", "unknown escape sequence" },        /* braced but empty */
    { "'\\u{zz}'", "unknown escape sequence" },      /* not hex */
    { "'\\u{03C0'", "unknown escape sequence" },     /* unterminated brace */
    /* More digits than any codepoint needs. Rejected on the digit count
     * rather than the value, so the accumulator cannot wrap first. */
    { "'\\u{1234567}'", "unknown escape sequence" },

    /* Readable, but not a codepoint. */
    { "'\\u{110000}'", "is not a Unicode codepoint" }, /* one past the last */
    { "'\\u{D800}'", "is not a Unicode codepoint" },   /* surrogate half */
    { "'\\u{DFFF}'", "is not a Unicode codepoint" },   /* the other end */
    { "'\\u{FFFFFF}'", "is not a Unicode codepoint" }, /* far out of range */
  };

  OAK_EXPECT_LEX_ERROR_CASES(cases);
}

/* The boundaries either side of each rejected range are accepted, so the
 * checks above are off-by-one free rather than merely strict. */
UTEST_F(utf8, unicode_escape_boundaries_are_accepted)
{
  static const oak_case_t cases[] = {
    { "let a = '\\u{0}';\n", OAK_NULL },       /* the first codepoint */
    { "let a = '\\u{D7FF}';\n", OAK_NULL },    /* just below the surrogates */
    { "let a = '\\u{E000}';\n", OAK_NULL },    /* just above them */
    { "let a = '\\u{10FFFF}';\n", OAK_NULL },  /* the last codepoint */
  };

  OAK_EXPECT_OK_CASES(cases);
}

/* Invalid encodings are rejected rather than passed through. Letting any of
 * these reach a string would put bytes in the heap that no later stage can
 * decode, so the failure has to happen here. */
UTEST_F(utf8, invalid_byte_sequences_are_rejected)
{
  static const oak_case_t cases[] = {
    /* A continuation byte with nothing to continue. */
    { "'\x80'", "not valid UTF-8" },
    /* A lead byte whose sequence never arrives. */
    { "'\xC3'", "not valid UTF-8" },
    /* Lead byte followed by a byte that is not a continuation. */
    { "'\xC3\x28'", "not valid UTF-8" },
    /* A 3-byte sequence cut short. */
    { "'\xE2\x98'", "not valid UTF-8" },
    /* Overlong encodings: legal-looking bytes for a codepoint that has a
     * shorter form. Accepting one is a classic way to smuggle a character
     * past a filter that only checks the canonical spelling. */
    { "'\xC0\xAF'", "not valid UTF-8" },
    { "'\xE0\x80\xAF'", "not valid UTF-8" },
    /* U+D800 encoded as though it were an ordinary codepoint. */
    { "'\xED\xA0\x80'", "not valid UTF-8" },
    /* Past U+10FFFF. */
    { "'\xF5\x80\x80\x80'", "not valid UTF-8" },
    /* Outside a literal too, as a bare source byte. */
    { "\x80", "invalid UTF-8 byte 0x80" },
    /* And in an identifier position, where the ident scanner sees it first. */
    { "let \xFF = 1;", "invalid UTF-8 byte 0xFF" },
  };

  OAK_EXPECT_LEX_ERROR_CASES(cases);
}

/*
 * Detecting bad input is only half of it: the build has to fail. The lexer
 * reports through the log and hands back a token stream with the offending
 * text simply missing, so a caller that does not check its error count runs a
 * program the author never wrote -- which is exactly what used to happen to a
 * stray non-UTF-8 byte between two statements.
 */
UTEST_F(utf8, bad_input_fails_the_compile_rather_than_being_skipped)
{
  static const oak_case_t cases[] = {
    /* The byte sits between two otherwise valid statements, so the lexer
     * recovers and the parser would see a perfectly good program. */
    { "let x = 1;\n\x80\nprint(x);\n", "lexical error" },
    { "let x = '\xC3';\n", "lexical error" },
    { "let x = '\\u{D800}';\n", "lexical error" },
    { "let x = '\\q';\n", "lexical error" },
  };

  OAK_EXPECT_COMPILE_ERROR_CASES(cases);
}

/* -------------------------------------------------------------- runtime -- */

/* size() is a character count. It has to be, because it is the bound scripts
 * loop to and hand back to substring(). */
UTEST_F(utf8, size_counts_characters_not_bytes)
{
  static const oak_case_t cases[] = {
    { "print('" HELLO "'.size());\n", "5" },
    { "print('" U_PI "'.size());\n", "1" },
    { "print('" U_SNOWMAN "'.size());\n", "1" },
    { "print('" U_GRIN "'.size());\n", "1" },
    { "print('" U_GRIN U_GRIN "'.size());\n", "2" },
    { "print('a" U_GRIN "b'.size());\n", "3" },
    /* ASCII is unaffected: the two counts coincide there. */
    { "print('hello'.size());\n", "5" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* substring() takes character indices, so a slice always lands on a character
 * boundary and never emits half of one. */
UTEST_F(utf8, substring_slices_on_character_boundaries)
{
  static const oak_case_t cases[] = {
    { "print('" HELLO "'.substring(0, 2));\n", "h" U_EACUTE },
    { "print('" HELLO "'.substring(1, 2));\n", U_EACUTE },
    { "print('" HELLO "'.substring(2, 5));\n", "llo" },
    { "print('" HELLO "'.substring(0, 5));\n", HELLO },
    /* Clamping is in characters too, so an over-long end is still the whole
     * string rather than a read past it. */
    { "print('" HELLO "'.substring(1, 100));\n", U_EACUTE "llo" },
    { "print('[' + '" HELLO "'.substring(3, 1) + ']');\n", "[]" },
    /* A 4-byte character is one index wide, like any other. */
    { "print('a" U_GRIN "b'.substring(1, 2));\n", U_GRIN },
    { "print('a" U_GRIN "b'.substring(2, 3));\n", "b" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* The end-to-end version of the property above: walking a string one index at
 * a time and gluing it back together reproduces it exactly. This is the idiom
 * scripts actually use, and it only works if size(), substring(), and the
 * concatenation all agree on what a character is. */
UTEST_F(utf8, character_by_character_round_trip_reproduces_the_string)
{
  static const oak_case_t cases[] = {
    { "let s = '" HELLO U_GRIN U_SNOWMAN "';\n"
      "let out = '';\n"
      "for i from 0 to s.size() {\n"
      "  out = out + s.substring(i, i + 1);\n"
      "}\n"
      "print(out);\n",
      HELLO U_GRIN U_SNOWMAN },
    /* Reversing exercises the same machinery without letting a no-op
     * implementation of substring() pass. */
    { "let s = 'a" U_EACUTE U_GRIN "';\n"
      "let out = '';\n"
      "for i from 0 to s.size() {\n"
      "  out = s.substring(i, i + 1) + out;\n"
      "}\n"
      "print(out);\n",
      U_GRIN U_EACUTE "a" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* index_of() reports a character index, so its result can be fed straight to
 * substring(). Searching is still byte-wise underneath -- UTF-8 is
 * self-synchronizing, so a byte match cannot straddle a boundary -- but the
 * number that comes back has to be in the same units as everything else. */
UTEST_F(utf8, index_of_reports_character_positions)
{
  static const oak_case_t cases[] = {
    { "print('" HELLO "'.index_of('llo'));\n", "2" },
    { "print('" HELLO "'.index_of('" U_EACUTE "'));\n", "1" },
    { "print('a" U_GRIN "b'.index_of('b'));\n", "2" },
    { "print('" HELLO "'.index_of('z'));\n", "-1" },
    /* The index round-trips through substring(). */
    { "let s = '" HELLO "';\n"
      "print(s.substring(s.index_of('" U_EACUTE "'), s.size()));\n",
      U_EACUTE "llo" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* ord() and chr() speak in codepoints, so every character round-trips rather
 * than only the ASCII ones. */
UTEST_F(utf8, ord_and_chr_round_trip_codepoints)
{
  static const oak_case_t cases[] = {
    { "print(ord('A'));\n", "65" },
    { "print(ord('" U_EACUTE "'));\n", "233" },
    { "print(ord('" U_PI "'));\n", "960" },
    { "print(ord('" U_SNOWMAN "'));\n", "9731" },
    { "print(ord('" U_GRIN "'));\n", "128512" },
    /* ord() reads the first character, not the first byte. */
    { "print(ord('" U_PI "abc'));\n", "960" },
    { "print(chr(960));\n", U_PI },
    { "print(chr(128512));\n", U_GRIN },
    { "print(chr(ord('" U_GRIN "')));\n", U_GRIN },
    /* The boundaries of each encoding width. */
    { "print(chr(127) + '|' + chr(128));\n", "\x7F" "|" "\xC2\x80" },
    { "print(chr(2047) + '|' + chr(2048));\n",
      "\xDF\xBF" "|" "\xE0\xA0\x80" },
    { "print(chr(65535) + '|' + chr(65536));\n",
      "\xEF\xBF\xBF" "|" "\xF0\x90\x80\x80" },
    { "print(chr(1114111));\n", "\xF4\x8F\xBF\xBF" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* chr() must refuse anything UTF-8 cannot encode, or it would be a way to
 * manufacture a string the rest of the language cannot decode. */
UTEST_F(utf8, chr_rejects_values_that_are_not_codepoints)
{
  static const oak_case_t cases[] = {
    { "print(chr(-1));\n", "-1 is not a codepoint" },
    { "print(chr(1114112));\n", "1114112 is not a codepoint" },
    { "print(chr(55296));\n", "55296 is a surrogate half" },
    { "print(chr(57343));\n", "57343 is a surrogate half" },
  };

  OAK_EXPECT_RUNTIME_ERROR_CASES(cases);
}

/* Case conversion is ASCII-only, which is the honest thing to be without
 * Unicode case tables. What matters is that it leaves multibyte characters
 * exactly as it found them instead of mangling their bytes. */
UTEST_F(utf8, case_conversion_leaves_multibyte_characters_intact)
{
  static const oak_case_t cases[] = {
    { "print('" HELLO "'.upper());\n", "H" U_EACUTE "LLO" },
    { "print('H" U_EACUTE "LLO'.lower());\n", HELLO },
    { "print('" U_GRIN "abc'.upper());\n", U_GRIN "ABC" },
    { "print('" U_PI "'.upper());\n", U_PI },
    { "print('" U_PI "'.lower());\n", U_PI },
    /* Case style conversion has the same obligation. */
    { "print('" U_GRIN "Hello" U_GRIN "World'.to_snake_case());\n",
      U_GRIN "hello" U_GRIN "world" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* trim() strips ASCII whitespace. A multibyte character next to the padding
 * must survive: its bytes are all >= 0x80 and none of them is whitespace, so
 * a byte-wise scan has to stop at it. */
UTEST_F(utf8, trim_stops_at_multibyte_characters)
{
  static const oak_case_t cases[] = {
    { "print('[' + '  " U_GRIN "  '.trim() + ']');\n", "[" U_GRIN "]" },
    { "print('[' + '\\t" HELLO "\\n'.trim() + ']');\n", "[" HELLO "]" },
    { "print('[' + '" U_PI "'.trim() + ']');\n", "[" U_PI "]" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* The byte-wise methods -- search, comparison, replacement, concatenation --
 * are safe on UTF-8 as written, but only as long as they keep whole
 * characters together. */
UTEST_F(utf8, byte_wise_methods_respect_character_boundaries)
{
  static const oak_case_t cases[] = {
    { "print('" HELLO "'.contains('" U_EACUTE "'));\n", "true" },
    { "print('" HELLO "'.starts_with('h" U_EACUTE "'));\n", "true" },
    { "print('" HELLO "'.ends_with('llo'));\n", "true" },
    /* The second byte of e-acute (0xA9) is also the second byte of U+00A9,
     * but a search must not match on a fragment of a character. */
    { "print('" U_EACUTE "'.contains('\\u{a9}'));\n", "false" },
    { "print('" U_EACUTE "'.replace('" U_EACUTE "', 'e'));\n", "e" },
    { "print('" U_GRIN "'.repeat(3));\n", U_GRIN U_GRIN U_GRIN },
    { "print('" U_PI "' + '" U_GRIN "');\n", U_PI U_GRIN },
    { "print('{} and {}'.format(['" U_PI "', '" U_GRIN "']));\n",
      U_PI " and " U_GRIN },
    /* Equality is byte equality, which is what UTF-8 makes correct for text
     * that is already in the same normal form. */
    { "print('" U_GRIN "' == '\\u{1F600}');\n", "true" },
    { "print('" U_PI "' == '" U_SNOWMAN "');\n", "false" },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}

/* Non-ASCII text must survive the parts of the runtime that are not string
 * methods: identifiers naming values, map keys, and collection printing. */
UTEST_F(utf8, multibyte_text_survives_the_wider_runtime)
{
  static const oak_case_t cases[] = {
    /* An identifier that is not ASCII names a value like any other. */
    { "let caf" U_EACUTE " = '" U_PI "';\nprint(caf" U_EACUTE ");\n", U_PI },
    /* Map keys are hashed and compared as bytes, so multibyte keys work and
     * two spellings of the same character find the same entry. */
    { "let m = new [string:number];\n"
      "m['" U_GRIN "'] = 7;\n"
      "print(m['\\u{1F600}']);\n",
      "7" },
    { "let m = new [string:number];\n"
      "m['" U_SNOWMAN "'] = 1;\n"
      "print(m.has('" U_SNOWMAN "'));\n"
      "print(m.has('" U_PI "'));\n",
      "true\nfalse" },
    /* Iteration hands back the strings unchanged. */
    { "let a = new string[];\n"
      "a.push('" U_PI "');\n"
      "a.push('" U_GRIN "');\n"
      "let out = '';\n"
      "for s in a {\n"
      "  out = out + s;\n"
      "}\n"
      "print(out);\n",
      U_PI U_GRIN },
  };

  OAK_EXPECT_OUTPUT_CASES(cases);
}
