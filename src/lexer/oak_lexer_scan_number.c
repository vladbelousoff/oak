#include "oak_lexer_internal.h"

#include <stdio.h>
#include <string.h>

enum oak_lex_status_t
oak_lexer_try_scan_number(const struct oak_lexer_ctx_t* ctx, const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  /* Save start positions */
  const struct oak_lexer_cur_t sav_cur = *cur;

  const char* const end = input + ctx->input_len;
  const char* p = start;
  int has_dot = 0;
  int has_exp = 0;

  // At least one digit
  if (p >= end || *p < '0' || *p > '9')
    return OAK_LEX_NO_MATCH;

  while (p < end)
  {
    const char c = *p;

    if (c >= '0' && c <= '9')
    {
      p++;
      oak_lexer_advance_cursor(cur, 1, 1);
    }
    else if (c == '.' && !has_dot && !has_exp)
    {
      has_dot = 1;
      p++;
      oak_lexer_advance_cursor(cur, 1, 1);
    }
    else if ((c == 'e' || c == 'E') && !has_exp)
    {
      has_exp = 1;
      p++;
      oak_lexer_advance_cursor(cur, 1, 1);

      // Must have at least one digit after e/E
      if (p >= end || *p < '0' || *p > '9')
        return OAK_LEX_NUMBER_SYNTAX;
    }
    else
    {
      break;
    }
  }

  const usize len = (usize)(p - start);
  if (len == 0)
    return OAK_LEX_NO_MATCH;

  /* Thread-local buffer to store number as string */
  static _Thread_local char tls_buffer[64];
  if (len >= sizeof(tls_buffer))
    return OAK_LEX_NUMBER_TOO_LONG;

  memset(tls_buffer, 0, sizeof(tls_buffer));
  memcpy(tls_buffer, start, len);
  tls_buffer[len] = '\0';

  if (has_dot || has_exp)
  {
    /* Floating-point */
    float val = 0.0f;
    if (sscanf(tls_buffer, "%f", &val) != 1)
      return OAK_LEX_NUMBER_SYNTAX;

    oak_lexer_save_token(
        ctx->lexer, &sav_cur, OAK_TOKEN_FLOAT, (char*)&val, sizeof(float));
  }
  else
  {
    /* Integer */
    int val = 0;
    if (sscanf(tls_buffer, "%d", &val) != 1)
      return OAK_LEX_NUMBER_SYNTAX;

    oak_lexer_save_token(
        ctx->lexer, &sav_cur, OAK_TOKEN_INT, (char*)&val, sizeof(int));
  }

  return OAK_LEX_OK;
}
