#pragma once

#include "oak_arena.h"
#include "oak_lexer_status.h"
#include "oak_token.h"
#include <oak_lexer.h>

typedef struct oak_lexer_result oak_lexer_result_t;
struct oak_lexer_result
{
  oak_list_entry_t tokens;
  oak_arena_t arena;
  int error_count;
  oak_allocator_t* allocator;
};

typedef struct oak_lexer_cur oak_lexer_cur_t;
struct oak_lexer_cur
{
  int buf_pos;
  int pos;
  int line;
  int column;
};

typedef struct oak_lexer_ctx oak_lexer_ctx_t;
struct oak_lexer_ctx
{
  oak_lexer_result_t* lexer;
  oak_lexer_cur_t* cur;
  usize input_len;
};

#define OAK_LEXER_TLS_BUF 64

typedef struct oak_growable_buf oak_growable_buf_t;
struct oak_growable_buf
{
  char* data;
  usize len;
  usize cap;
  int heap;
  oak_allocator_t* allocator;
};

oak_lexer_result_t* oak_lexer_tokenize_len(
    const char* input, usize len, oak_allocator_t* allocator);

void oak_growable_buf_init(oak_growable_buf_t* b,
                           char tls[OAK_LEXER_TLS_BUF],
                           oak_allocator_t* allocator);
void oak_growable_buf_free(oak_growable_buf_t* b);
oak_lex_status_t oak_growable_buf_reserve(oak_growable_buf_t* b,
                                               usize min_cap);

void oak_lexer_advance_cursor(oak_lexer_cur_t* cur, int n, int bytes);

void oak_lexer_new_line(oak_lexer_cur_t* cur);

void oak_lexer_save_token(oak_lexer_result_t* lexer,
                          const oak_lexer_cur_t* cur,
                          oak_token_kind_t token_kind,
                          const char* buffer,
                          usize buffer_size);
