#include "oak_lexer_internal.h"

#include "oak_utf8.h"

#include <string.h>

enum oak_lex_status_t
oak_lexer_try_scan_string(const struct oak_lexer_ctx_t* ctx, const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  if (*start != '\'')
  {
    return OAK_LEX_NO_MATCH;
  }

  const struct oak_lexer_cur_t sav_cur = *cur;
  oak_lexer_advance_cursor(cur, 1, 1);

  char tls[OAK_LEXER_TLS_BUF];
  struct oak_growable_buf_t gb;
  oak_growable_buf_init(&gb, tls);

  const char* const end = input + ctx->input_len;
  const char* p = start + 1;
  if (p < end && *p == '\'')
  {
    oak_lexer_advance_cursor(cur, 1, 1);
    oak_lexer_save_token(ctx->lexer, &sav_cur, OAK_TOKEN_STRING, tls, 0);
    return OAK_LEX_OK;
  }
  while (p < end)
  {
    u32 cp;
    int n = oak_utf8_next_bounded(p, (usize)(end - p), &cp);
    if (n < 0)
    {
      oak_growable_buf_free(&gb);
      return OAK_LEX_INVALID_UTF8;
    }
    if (n == 0)
    {
      oak_growable_buf_free(&gb);
      return OAK_LEX_UNTERMINATED_STRING;
    }

    if (cp == '\\')
    {
      p += n;
      oak_lexer_advance_cursor(cur, 1, n);

      if (p >= end)
        break;

      switch (*p)
      {
        case 'n':
          cp = '\n';
          break;
        case 't':
          cp = '\t';
          break;
        case 'r':
          cp = '\r';
          break;
        case '\\':
        case '\'':
        case '"':
        default:
          cp = (u8)*p;
          break;
      }
      n = 1;
    }

    {
      const enum oak_lex_status_t st =
          oak_growable_buf_reserve(&gb, gb.len + 4u);
      if (st != OAK_LEX_OK)
      {
        oak_growable_buf_free(&gb);
        return st;
      }
    }

    const int written = oak_utf8_encode(cp, gb.data + gb.len);
    gb.len += (usize)written;

    p += n;
    oak_lexer_advance_cursor(cur, 1, n);

    if (*p == '\'')
    {
      oak_lexer_advance_cursor(cur, 1, 1);
      oak_lexer_save_token(
          ctx->lexer, &sav_cur, OAK_TOKEN_STRING, gb.data, gb.len);
      oak_growable_buf_free(&gb);
      return OAK_LEX_OK;
    }
  }

  oak_growable_buf_free(&gb);
  return OAK_LEX_UNTERMINATED_STRING;
}
