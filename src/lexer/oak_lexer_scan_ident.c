#include "oak_lexer_internal.h"

#include "oak_utf8.h"

#include <string.h>

enum oak_lex_status_t
oak_lexer_try_scan_ident(const struct oak_lexer_ctx_t* ctx, const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];
  const struct oak_lexer_cur_t sav_cur = *cur;

  const char* const end = input + ctx->input_len;
  if (start >= end)
    return OAK_LEX_NO_MATCH;

  u32 cp = 0;
  int n = oak_utf8_next_bounded(start, (usize)(end - start), &cp);
  if (n <= 0)
    return OAK_LEX_NO_MATCH;

  if (!(oak_utf8_is_alpha(cp) || cp == '_'))
    return OAK_LEX_NO_MATCH;

  const char* p = start;
  char tls[OAK_LEXER_TLS_BUF];
  struct oak_growable_buf_t gb;
  oak_growable_buf_init(&gb, tls);

  while (p < end)
  {
    n = oak_utf8_next_bounded(p, (usize)(end - p), &cp);
    if (n <= 0)
      break;

    if (!(oak_utf8_is_alpha(cp) || (cp >= '0' && cp <= '9') || cp == '_'))
      break;

    {
      const enum oak_lex_status_t st =
          oak_growable_buf_reserve(&gb, gb.len + (usize)n);
      if (st != OAK_LEX_OK)
      {
        oak_growable_buf_free(&gb);
        return st;
      }
    }

    memcpy(gb.data + gb.len, p, (usize)n);
    gb.len += (usize)n;

    p += n;
    oak_lexer_advance_cursor(cur, 1, n);
  }

  if (gb.len == 0u)
  {
    oak_growable_buf_free(&gb);
    return OAK_LEX_NO_MATCH;
  }

  const enum oak_token_kind_t kind = oak_keyword_lookup(gb.data, gb.len);
  oak_lexer_save_token(ctx->lexer,
                       &sav_cur,
                       kind,
                       gb.data,
                       kind == OAK_TOKEN_IDENT ? gb.len : 0u);

  oak_growable_buf_free(&gb);
  return OAK_LEX_OK;
}
