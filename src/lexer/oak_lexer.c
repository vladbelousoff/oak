#include "oak_lexer_internal.h"

#include "oak_log.h"
#include "oak_mem.h"
#include "oak_utf8.h"

#include <string.h>

void oak_lexer_advance_cursor(struct oak_lexer_cur_t* cur,
                              const int n,
                              const int bytes)
{
  cur->buf_pos += bytes;
  cur->column += n;
  cur->pos += n;
}

void oak_lexer_new_line(struct oak_lexer_cur_t* cur)
{
  cur->line++;
  cur->column = 0;
}

void oak_lexer_save_token(struct oak_lexer_result_t* lexer,
                          const struct oak_lexer_cur_t* cur,
                          const enum oak_token_kind_t token_kind,
                          const char* buffer,
                          const usize buffer_size)
{
  usize token_size = sizeof(struct oak_token_t);
  if (buffer_size > 0)
  {
    token_size += buffer_size + 1;
  }
  else if (token_kind == OAK_TOKEN_STRING)
  {
    token_size += 1;
  }

  struct oak_token_t* token = oak_arena_alloc(&lexer->arena, token_size);
  token->kind = token_kind;
  token->line = cur->line;
  token->column = cur->column;
  token->offset = cur->pos;
  token->length = (int)buffer_size;

  if (buffer && buffer_size > 0)
  {
    memcpy(token->text, buffer, buffer_size);
  }

  if (buffer_size > 0)
  {
    token->text[buffer_size] = '\0';
  }
  else if (token_kind == OAK_TOKEN_STRING)
  {
    token->text[0] = '\0';
  }

  oak_list_add_tail(&lexer->tokens, &token->link);
}

static struct
{
  enum oak_lex_status_t (*try_scan)(const struct oak_lexer_ctx_t* ctx,
                                    const char* input);
} scanners[] = {
  { oak_lexer_try_scan_ws },     { oak_lexer_try_scan_op },
  { oak_lexer_try_scan_string }, { oak_lexer_try_scan_number },
  { oak_lexer_try_scan_ident },  { null },
};

static enum oak_lex_status_t try_scan(const struct oak_lexer_ctx_t* ctx,
                                      const char* input)
{
  for (int i = 0; scanners[i].try_scan; ++i)
  {
    const enum oak_lex_status_t r = scanners[i].try_scan(ctx, input);
    if (r == OAK_LEX_OK)
      return OAK_LEX_OK;
    if (r != OAK_LEX_NO_MATCH)
      return r;
  }

  return OAK_LEX_NO_MATCH;
}

struct oak_lexer_result_t* oak_lexer_tokenize(const char* input,
                                              const usize len)
{
  if (len > 0 && !input)
    return null;

  struct oak_lexer_result_t* result =
      oak_alloc(sizeof(struct oak_lexer_result_t), OAK_SRC_LOC);
  if (!result)
    return null;

  struct oak_lexer_cur_t cur = {
    .buf_pos = 0, .pos = 1, .line = 1, .column = 1
  };
  const struct oak_lexer_ctx_t ctx = { .lexer = result,
                                       .cur = &cur,
                                       .input_len = len };
  oak_list_init(&result->tokens);
  oak_arena_init(&result->arena, 0);

  while ((usize)cur.buf_pos < len)
  {
    if (input[cur.buf_pos] == '\0')
      break;

    const enum oak_lex_status_t step = try_scan(&ctx, input);
    if (step == OAK_LEX_OK)
      continue;

    if (step == OAK_LEX_NO_MATCH)
    {
      u32 cp = 0;
      const usize rem = len - (usize)cur.buf_pos;
      const int n = oak_utf8_next_bounded(&input[cur.buf_pos], rem, &cp);
      oak_log_cond(n < 0, OAK_LOG_ERROR, "invalid utf8 character: 0x%.8X", cp);
    }
    else
    {
      oak_log(OAK_LOG_ERROR, "lexer: status %d", (int)step);
      break;
    }
  }

  return result;
}

const struct oak_list_entry_t*
oak_lexer_tokens(const struct oak_lexer_result_t* result)
{
  return result ? &result->tokens : null;
}

void oak_lexer_free(struct oak_lexer_result_t* result)
{
  if (!result)
    return;
  oak_arena_free(&result->arena);
  oak_free(result, OAK_SRC_LOC);
}
