#include "oak_lexer_internal.h"

#include "oak_utf8.h"

enum oak_lex_status_t oak_lexer_try_scan_ws(const struct oak_lexer_ctx_t* ctx,
                                            const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  for (;;)
  {
    if ((usize)cur->buf_pos >= ctx->input_len)
      break;

    const usize rem = ctx->input_len - (usize)cur->buf_pos;
    u32 cp = 0;
    const int n = oak_utf8_next_bounded(&input[cur->buf_pos], rem, &cp);

    if (n < 0)
      break;
    if (n == 0)
      break;

    if (cp == ' ' || cp == '\t' || cp == '\r')
    {
      oak_lexer_advance_cursor(cur, 1, n);
      continue;
    }

    if (cp == '\n')
    {
      oak_lexer_new_line(cur);
      oak_lexer_advance_cursor(cur, 1, n);
      continue;
    }

    break;
  }

  return OAK_LEX_NO_MATCH;
}
