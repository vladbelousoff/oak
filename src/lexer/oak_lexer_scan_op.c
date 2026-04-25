#include "oak_lexer_internal.h"

#include "oak_count_of.h"

struct oak_two_char_op_t
{
  char a, b;
  enum oak_token_kind_t token;
};

struct oak_single_char_op_t
{
  char c;
  enum oak_token_kind_t token;
};

/* Two-character operators table */
static const struct oak_two_char_op_t two_char_ops[] = {
  { '=', '=', OAK_TOKEN_EQUAL_EQUAL },  { '!', '=', OAK_TOKEN_BANG_EQUAL },
  { '-', '>', OAK_TOKEN_ARROW },        { '&', '&', OAK_TOKEN_AND },
  { '|', '|', OAK_TOKEN_OR },           { '>', '=', OAK_TOKEN_GREATER_EQUAL },
  { '<', '=', OAK_TOKEN_LESS_EQUAL },   { '+', '=', OAK_TOKEN_PLUS_ASSIGN },
  { '-', '=', OAK_TOKEN_MINUS_ASSIGN }, { '*', '=', OAK_TOKEN_STAR_ASSIGN },
  { '/', '=', OAK_TOKEN_SLASH_ASSIGN }, { '%', '=', OAK_TOKEN_PERCENT_ASSIGN },
};

/* Single-character operators table */
static const struct oak_single_char_op_t single_char_ops[] = {
  { ':', OAK_TOKEN_COLON },     { ',', OAK_TOKEN_COMMA },
  { ';', OAK_TOKEN_SEMICOLON }, { '=', OAK_TOKEN_ASSIGN },
  { '!', OAK_TOKEN_BANG },      { '-', OAK_TOKEN_MINUS },
  { '+', OAK_TOKEN_PLUS },      { '*', OAK_TOKEN_STAR },
  { '/', OAK_TOKEN_SLASH },     { '%', OAK_TOKEN_PERCENT },
  { '(', OAK_TOKEN_LPAREN },    { ')', OAK_TOKEN_RPAREN },
  { '{', OAK_TOKEN_LBRACE },    { '}', OAK_TOKEN_RBRACE },
  { '[', OAK_TOKEN_LBRACKET },  { ']', OAK_TOKEN_RBRACKET },
  { '>', OAK_TOKEN_GREATER },   { '<', OAK_TOKEN_LESS },
  { '.', OAK_TOKEN_DOT },       { '?', OAK_TOKEN_QUESTION },
};

enum oak_lex_status_t oak_lexer_try_scan_op(const struct oak_lexer_ctx_t* ctx,
                                            const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  if ((usize)cur->buf_pos >= ctx->input_len)
    return OAK_LEX_NO_MATCH;

  const char* p = &input[cur->buf_pos];
  const char c1 = p[0];
  const char c2 = (usize)cur->buf_pos + 1 < ctx->input_len ? p[1] : '\0';
  usize i;

  /* Save start positions */
  const struct oak_lexer_cur_t sav_cur = *cur;

  /* Check two-character operators first */
  for (i = 0; i < sizeof(two_char_ops) / sizeof(two_char_ops[0]); ++i)
  {
    if (c1 == two_char_ops[i].a && c2 == two_char_ops[i].b)
    {
      const enum oak_token_kind_t token = two_char_ops[i].token;
      oak_lexer_save_token(ctx->lexer, &sav_cur, token, p, 0);
      oak_lexer_advance_cursor(cur, 2, 2);
      return OAK_LEX_OK;
    }
  }

  /* Check single-character operators */
  for (i = 0; i < oak_count_of(single_char_ops); ++i)
  {
    if (c1 == single_char_ops[i].c)
    {
      const enum oak_token_kind_t token = single_char_ops[i].token;
      oak_lexer_save_token(ctx->lexer, &sav_cur, token, p, 0);
      oak_lexer_advance_cursor(cur, 1, 1);
      return OAK_LEX_OK;
    }
  }

  return OAK_LEX_NO_MATCH;
}
