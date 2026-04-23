#include "oak_lexer_internal.h"

#include "oak_mem.h"

#include <string.h>

void oak_growable_buf_init(struct oak_growable_buf_t* b, char tls[OAK_LEXER_TLS_BUF])
{
  b->data = tls;
  b->len = 0u;
  b->cap = (usize)OAK_LEXER_TLS_BUF;
  b->heap = 0;
  memset(tls, 0, (usize)OAK_LEXER_TLS_BUF);
}

void oak_growable_buf_free(struct oak_growable_buf_t* b)
{
  if (b->heap)
    oak_free(b->data, OAK_SRC_LOC);
  b->data = null;
  b->len = 0u;
  b->cap = 0u;
  b->heap = 0;
}

enum oak_lex_status_t
oak_growable_buf_reserve(struct oak_growable_buf_t* b, const usize min_cap)
{
  if (min_cap <= b->cap)
    return OAK_LEX_OK;
  if (!b->heap)
  {
    usize new_cap = (usize)OAK_LEXER_TLS_BUF * 2u;
    while (new_cap < min_cap)
      new_cap *= 2u;
    char* new_buf = oak_alloc(new_cap, OAK_SRC_LOC);
    if (!new_buf)
      return OAK_LEX_ALLOC_FAILED;
    memcpy(new_buf, b->data, b->len);
    b->data = new_buf;
    b->cap = new_cap;
    b->heap = 1;
    return OAK_LEX_OK;
  }
  usize new_cap = b->cap * 2u;
  while (new_cap < min_cap)
    new_cap *= 2u;
  char* new_buf = oak_realloc(b->data, new_cap, OAK_SRC_LOC);
  if (!new_buf)
  {
    oak_free(b->data, OAK_SRC_LOC);
    b->data = null;
    return OAK_LEX_ALLOC_FAILED;
  }
  b->data = new_buf;
  b->cap = new_cap;
  return OAK_LEX_OK;
}
