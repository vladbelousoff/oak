#include "oak_lexer_internal.h"

#include "oak_mem.h"
#include "oak_utf8.h"

#include <string.h>

enum oak_lex_status_t
oak_lexer_try_scan_string(const struct oak_lexer_ctx_t* ctx, const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  /* Not a string literal */
  if (*start != '\'')
  {
    return OAK_LEX_NO_MATCH;
  }

  /* Save start positions */
  const struct oak_lexer_cur_t sav_cur = *cur;

  /* Skip opening quote */
  oak_lexer_advance_cursor(cur, 1, 1);

  /* Thread-local static buffer first */
  static _Thread_local char tls_buffer[64];
  char* buffer = tls_buffer;
  usize buffer_capacity = sizeof(tls_buffer);
  usize buffer_length = 0;
  int dynamic_alloc = 0;

  const char* const end = input + ctx->input_len;
  const char* p = start + 1;
  /* `''` — empty string (closing quote must not be consumed as content). */
  if (p < end && *p == '\'')
  {
    oak_lexer_advance_cursor(cur, 1, 1);
    oak_lexer_save_token(ctx->lexer, &sav_cur, OAK_TOKEN_STRING, tls_buffer, 0);
    return OAK_LEX_OK;
  }
  while (p < end)
  {
    u32 cp;
    int n = oak_utf8_next_bounded(p, (usize)(end - p), &cp);
    if (n < 0)
    {
      if (dynamic_alloc)
        oak_free(buffer, OAK_SRC_LOC);
      /* Invalid UTF-8 */
      return OAK_LEX_INVALID_UTF8;
    }
    if (n == 0)
    {
      if (dynamic_alloc)
        oak_free(buffer, OAK_SRC_LOC);
      return OAK_LEX_UNTERMINATED_STRING;
    }

    /* Handle escape sequences */
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

    /* Resize buffer if needed */
    if (buffer_length + 4 >= buffer_capacity)
    {
      if (!dynamic_alloc)
      {
        buffer_capacity = sizeof(tls_buffer) * 2;
        char* new_buf = oak_alloc(buffer_capacity, OAK_SRC_LOC);
        if (!new_buf)
          return 1;
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

    const int written = oak_utf8_encode(cp, buffer + buffer_length);
    buffer_length += written;

    p += n;
    oak_lexer_advance_cursor(cur, 1, n);

    /* Closing quote found */
    if (*p == '\'')
    {
      oak_lexer_advance_cursor(cur, 1, 1);
      oak_lexer_save_token(
          ctx->lexer, &sav_cur, OAK_TOKEN_STRING, buffer, buffer_length);

      if (dynamic_alloc)
        oak_free(buffer, OAK_SRC_LOC);
      return OAK_LEX_OK;
    }
  }

  /* Unterminated string literal */
  if (dynamic_alloc)
    oak_free(buffer, OAK_SRC_LOC);
  return OAK_LEX_UNTERMINATED_STRING;
}
