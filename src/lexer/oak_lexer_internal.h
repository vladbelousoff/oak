#pragma once

#include "oak_arena.h"
#include "oak_lexer.h"
#include "oak_lexer_status.h"
#include "oak_token.h"

struct oak_lexer_result_t
{
  struct oak_list_entry_t tokens;
  struct oak_arena_t arena;
};

struct oak_lexer_cur_t
{
  int buf_pos;
  int pos;
  int line;
  int column;
};

struct oak_lexer_ctx_t
{
  struct oak_lexer_result_t* lexer;
  struct oak_lexer_cur_t* cur;
  usize input_len;
};

void oak_lexer_advance_cursor(struct oak_lexer_cur_t* cur,
                              int n,
                              int bytes);

void oak_lexer_new_line(struct oak_lexer_cur_t* cur);

void oak_lexer_save_token(struct oak_lexer_result_t* lexer,
                          const struct oak_lexer_cur_t* cur,
                          enum oak_token_kind_t token_kind,
                          const char* buffer,
                          usize buffer_size);

enum oak_lex_status_t oak_lexer_try_scan_ws(const struct oak_lexer_ctx_t* ctx,
                                            const char* input);

enum oak_lex_status_t oak_lexer_try_scan_op(const struct oak_lexer_ctx_t* ctx,
                                            const char* input);

enum oak_lex_status_t
oak_lexer_try_scan_string(const struct oak_lexer_ctx_t* ctx, const char* input);

enum oak_lex_status_t
oak_lexer_try_scan_number(const struct oak_lexer_ctx_t* ctx, const char* input);

enum oak_lex_status_t
oak_lexer_try_scan_ident(const struct oak_lexer_ctx_t* ctx, const char* input);
