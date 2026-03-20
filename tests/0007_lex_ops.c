#include "oak_lex.h"
#include "oak_test.h"
#include "oak_test_run.h"
#include "oak_test_tok.h"

OAK_TEST_DECL(LexOperators)
{
  oak_lex_result_t* lex = oak_lex_tokenize(
      "== != -> && || >= <= : , ; = ! - + * / ( ) { } [ ] > < . ?");

  static oak_tok_attr_t attrs[] = {
    { .type = OAK_TOK_EQUAL, .line = 1, .column = 1, .pos = 1 },
    { .type = OAK_TOK_NOT_EQUAL, .line = 1, .column = 4, .pos = 4 },
    { .type = OAK_TOK_ARROW, .line = 1, .column = 7, .pos = 7 },
    { .type = OAK_TOK_AND, .line = 1, .column = 10, .pos = 10 },
    { .type = OAK_TOK_OR, .line = 1, .column = 13, .pos = 13 },
    { .type = OAK_TOK_GREATER_EQUAL, .line = 1, .column = 16, .pos = 16 },
    { .type = OAK_TOK_LESS_EQUAL, .line = 1, .column = 19, .pos = 19 },
    { .type = OAK_TOK_COLON, .line = 1, .column = 22, .pos = 22 },
    { .type = OAK_TOK_COMMA, .line = 1, .column = 24, .pos = 24 },
    { .type = OAK_TOK_SEMICOLON, .line = 1, .column = 26, .pos = 26 },
    { .type = OAK_TOK_ASSIGN, .line = 1, .column = 28, .pos = 28 },
    { .type = OAK_TOK_EXCLAMATION_MARK, .line = 1, .column = 30, .pos = 30 },
    { .type = OAK_TOK_MINUS, .line = 1, .column = 32, .pos = 32 },
    { .type = OAK_TOK_PLUS, .line = 1, .column = 34, .pos = 34 },
    { .type = OAK_TOK_STAR, .line = 1, .column = 36, .pos = 36 },
    { .type = OAK_TOK_SLASH, .line = 1, .column = 38, .pos = 38 },
    { .type = OAK_TOK_LPAREN, .line = 1, .column = 40, .pos = 40 },
    { .type = OAK_TOK_RPAREN, .line = 1, .column = 42, .pos = 42 },
    { .type = OAK_TOK_LBRACE, .line = 1, .column = 44, .pos = 44 },
    { .type = OAK_TOK_RBRACE, .line = 1, .column = 46, .pos = 46 },
    { .type = OAK_TOK_LBRACKET, .line = 1, .column = 48, .pos = 48 },
    { .type = OAK_TOK_RBRACKET, .line = 1, .column = 50, .pos = 50 },
    { .type = OAK_TOK_GREATER, .line = 1, .column = 52, .pos = 52 },
    { .type = OAK_TOK_LESS, .line = 1, .column = 54, .pos = 54 },
    { .type = OAK_TOK_DOT, .line = 1, .column = 56, .pos = 56 },
    { .type = OAK_TOK_QUESTION_MARK, .line = 1, .column = 58, .pos = 58 },
  };

  const size_t n = OAK_ARRAY_SIZE(attrs);
  const oak_result_t result = oak_test_tokens(lex, attrs, n);
  oak_lex_cleanup(lex);
  return result;
}

OAK_TEST_MAIN(LexOperators)
