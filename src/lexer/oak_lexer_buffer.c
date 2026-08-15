#include "internal/oak_lexer.h"

#include <string.h>

void oak_growable_buf_init(oak_growable_buf_t* b,
                           char tls[OAK_LEXER_TLS_BUF],
                           oak_allocator_t* allocator)
{
  b->data = tls;
  b->len = 0u;
  b->cap = (usize)OAK_LEXER_TLS_BUF;
  b->heap = 0;
  b->allocator = allocator;
  memset(tls, 0, (usize)OAK_LEXER_TLS_BUF);
}

void oak_growable_buf_free(oak_growable_buf_t* b)
{
  if (b->heap)
    oak_free(b->allocator, b->data, OAK_HERE);
  b->data = OAK_NULL;
  b->len = 0u;
  b->cap = 0u;
  b->heap = 0;
}

oak_lex_status_t oak_growable_buf_reserve(oak_growable_buf_t* b,
                                               const usize min_cap)
{
  if (min_cap <= b->cap)
    return OAK_LEX_OK;
  if (!b->heap)
  {
    usize new_cap = (usize)OAK_LEXER_TLS_BUF * 2u;
    while (new_cap < min_cap)
      new_cap *= 2u;
    char* new_buf = oak_alloc(b->allocator, new_cap, OAK_HERE);
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
  char* new_buf = oak_realloc(b->allocator, b->data, new_cap, OAK_HERE);
  if (!new_buf)
  {
    oak_free(b->allocator, b->data, OAK_HERE);
    b->data = OAK_NULL;
    return OAK_LEX_ALLOC_FAILED;
  }
  b->data = new_buf;
  b->cap = new_cap;
  return OAK_LEX_OK;
}
