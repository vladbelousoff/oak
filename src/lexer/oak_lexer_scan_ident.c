#include "oak_lexer_internal.h"

#include "oak_mem.h"
#include "oak_utf8.h"

#include <string.h>

enum oak_lex_status_t
oak_lexer_try_scan_ident(const struct oak_lexer_ctx_t* ctx, const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  /* Save start positions */
  const struct oak_lexer_cur_t sav_cur = *cur;

  const char* const end = input + ctx->input_len;
  if (start >= end)
    return OAK_LEX_NO_MATCH;

  u32 cp = 0;
  int n = oak_utf8_next_bounded(start, (usize)(end - start), &cp);
  if (n <= 0)
    return OAK_LEX_NO_MATCH;

  /* Identifier must start with a letter or underscore */
  if (!(oak_utf8_is_alpha(cp) || cp == '_'))
    return OAK_LEX_NO_MATCH;

  const char* p = start;

  /* Thread-local static buffer first */
  static _Thread_local char tls_buffer[64];
  char* buffer = tls_buffer;
  usize buffer_capacity = sizeof(tls_buffer);
  usize buffer_length = 0;
  memset(buffer, 0, buffer_capacity);
  int dynamic_alloc = 0;

  while (p < end)
  {
    n = oak_utf8_next_bounded(p, (usize)(end - p), &cp);
    if (n <= 0)
      break;

    /* Valid identifier characters: letters, digits, underscore */
    if (!(oak_utf8_is_alpha(cp) || (cp >= '0' && cp <= '9') || cp == '_'))
      break;

    /* Resize buffer if needed */
    if (buffer_length + n >= buffer_capacity)
    {
      if (!dynamic_alloc)
      {
        buffer_capacity = sizeof(tls_buffer) * 2;
        char* new_buf = oak_alloc(buffer_capacity, OAK_SRC_LOC);
        memset(new_buf, 0, buffer_capacity);
        if (!new_buf)
          return OAK_LEX_ALLOC_FAILED;
        memcpy(new_buf, tls_buffer, buffer_length);
        buffer = new_buf;
        dynamic_alloc = 1;
      }
      else
      {
        buffer_capacity *= 2;
        char* new_buf = oak_realloc(buffer, buffer_capacity, OAK_SRC_LOC);
        if (!new_buf)
        {
          oak_free(buffer, OAK_SRC_LOC);
          return OAK_LEX_ALLOC_FAILED;
        }
        buffer = new_buf;
      }
    }

    memcpy(buffer + buffer_length, p, n);
    buffer_length += n;

    p += n;
    oak_lexer_advance_cursor(cur, 1, n);
  }

  if (buffer_length == 0)
  {
    if (dynamic_alloc)
      oak_free(buffer, OAK_SRC_LOC);
    return OAK_LEX_NO_MATCH;
  }

  const enum oak_token_kind_t kind = oak_keyword_lookup(buffer, buffer_length);
  oak_lexer_save_token(ctx->lexer,
                       &sav_cur,
                       kind,
                       buffer,
                       kind == OAK_TOKEN_IDENT ? buffer_length : 0);

  if (dynamic_alloc)
    oak_free(buffer, OAK_SRC_LOC);

  return OAK_LEX_OK;
}
