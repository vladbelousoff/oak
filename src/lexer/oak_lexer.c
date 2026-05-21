#include "internal/oak_lexer.h"

#include "oak_count_of.h"
#include "oak_log.h"
#include "oak_allocator.h"
#include "oak_utf8.h"

#include <stdio.h>
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
  token->length = buffer_size;

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

/* ---------- Scanners ---------- */

static enum oak_lex_status_t scan_ws(const struct oak_lexer_ctx_t* ctx,
                                     const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const int start = cur->buf_pos;
  for (;;)
  {
    if ((usize)cur->buf_pos >= ctx->input_len)
      break;

    const usize rem = ctx->input_len - (usize)cur->buf_pos;
    u32 cp = 0;
    const int n = oak_utf8_next_bounded(&input[cur->buf_pos], rem, &cp);

    if (n <= 0)
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

  return cur->buf_pos != start ? OAK_LEX_OK : OAK_LEX_NO_MATCH;
}

static enum oak_lex_status_t scan_block_comment(
    const struct oak_lexer_ctx_t* ctx, const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  if ((usize)cur->buf_pos + 1u >= ctx->input_len)
    return OAK_LEX_NO_MATCH;
  if (input[cur->buf_pos] != '/' || input[cur->buf_pos + 1] != '*')
    return OAK_LEX_NO_MATCH;

  oak_lexer_advance_cursor(cur, 2, 2);
  while ((usize)cur->buf_pos < ctx->input_len)
  {
    if ((usize)cur->buf_pos + 1u < ctx->input_len &&
        input[cur->buf_pos] == '*' && input[cur->buf_pos + 1] == '/')
    {
      oak_lexer_advance_cursor(cur, 2, 2);
      return OAK_LEX_OK;
    }
    if (input[cur->buf_pos] == '\n')
    {
      oak_lexer_new_line(cur);
      oak_lexer_advance_cursor(cur, 1, 1);
      continue;
    }
    oak_lexer_advance_cursor(cur, 1, 1);
  }

  return OAK_LEX_UNTERMINATED_COMMENT;
}

struct two_char_op_t
{
  char a, b;
  enum oak_token_kind_t token;
};

struct single_char_op_t
{
  char c;
  enum oak_token_kind_t token;
};

static const struct two_char_op_t two_char_ops[] = {
  { '=', '=', OAK_TOKEN_EQUAL_EQUAL },  { '!', '=', OAK_TOKEN_BANG_EQUAL },
  { '-', '>', OAK_TOKEN_ARROW },        { '&', '&', OAK_TOKEN_AND },
  { '|', '|', OAK_TOKEN_OR },           { '>', '=', OAK_TOKEN_GREATER_EQUAL },
  { '<', '=', OAK_TOKEN_LESS_EQUAL },   { '+', '=', OAK_TOKEN_PLUS_ASSIGN },
  { '-', '=', OAK_TOKEN_MINUS_ASSIGN }, { '*', '=', OAK_TOKEN_STAR_ASSIGN },
  { '/', '=', OAK_TOKEN_SLASH_ASSIGN }, { '%', '=', OAK_TOKEN_PERCENT_ASSIGN },
  { '/', '/', OAK_TOKEN_DOUBLE_SLASH },
};

static const struct single_char_op_t single_char_ops[] = {
  { ':', OAK_TOKEN_COLON },     { ',', OAK_TOKEN_COMMA },
  { ';', OAK_TOKEN_SEMICOLON }, { '=', OAK_TOKEN_ASSIGN },
  { '!', OAK_TOKEN_BANG },      { '-', OAK_TOKEN_MINUS },
  { '+', OAK_TOKEN_PLUS },      { '*', OAK_TOKEN_STAR },
  { '/', OAK_TOKEN_SLASH },     { '%', OAK_TOKEN_PERCENT },
  { '(', OAK_TOKEN_LPAREN },    { ')', OAK_TOKEN_RPAREN },
  { '{', OAK_TOKEN_LBRACE },    { '}', OAK_TOKEN_RBRACE },
  { '[', OAK_TOKEN_LBRACKET },  { ']', OAK_TOKEN_RBRACKET },
  { '>', OAK_TOKEN_GREATER },   { '<', OAK_TOKEN_LESS },
  { '.', OAK_TOKEN_DOT },       { '?', OAK_TOKEN_QUESTION },
  { '@', OAK_TOKEN_AT },
};

static enum oak_lex_status_t scan_op(const struct oak_lexer_ctx_t* ctx,
                                     const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  if ((usize)cur->buf_pos >= ctx->input_len)
    return OAK_LEX_NO_MATCH;

  const char* p = &input[cur->buf_pos];
  const char c1 = p[0];
  const char c2 = (usize)cur->buf_pos + 1 < ctx->input_len ? p[1] : '\0';
  const struct oak_lexer_cur_t sav_cur = *cur;

  for (usize i = 0; i < oak_count_of(two_char_ops); ++i)
  {
    if (c1 == two_char_ops[i].a && c2 == two_char_ops[i].b)
    {
      oak_lexer_save_token(ctx->lexer, &sav_cur, two_char_ops[i].token, p, 0);
      oak_lexer_advance_cursor(cur, 2, 2);
      return OAK_LEX_OK;
    }
  }

  for (usize i = 0; i < oak_count_of(single_char_ops); ++i)
  {
    if (c1 == single_char_ops[i].c)
    {
      oak_lexer_save_token(
          ctx->lexer, &sav_cur, single_char_ops[i].token, p, 0);
      oak_lexer_advance_cursor(cur, 1, 1);
      return OAK_LEX_OK;
    }
  }

  return OAK_LEX_NO_MATCH;
}

static enum oak_lex_status_t scan_string(const struct oak_lexer_ctx_t* ctx,
                                         const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  if (*start != '\'')
    return OAK_LEX_NO_MATCH;

  const struct oak_lexer_cur_t sav_cur = *cur;
  oak_lexer_advance_cursor(cur, 1, 1);

  char tls[OAK_LEXER_TLS_BUF];
  struct oak_growable_buf_t gb;
  oak_growable_buf_init(&gb, tls, ctx->lexer->allocator);

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

static enum oak_lex_status_t scan_number(const struct oak_lexer_ctx_t* ctx,
                                         const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];
  const struct oak_lexer_cur_t sav_cur = *cur;

  const char* const end = input + ctx->input_len;
  const char* p = start;
  int has_dot = 0;
  int has_exp = 0;

  if (p >= end || *p < '0' || *p > '9')
    return OAK_LEX_NO_MATCH;

  while (p < end)
  {
    const char c = *p;

    if (c >= '0' && c <= '9')
    {
      p++;
      oak_lexer_advance_cursor(cur, 1, 1);
    }
    else if (c == '.' && !has_dot && !has_exp)
    {
      has_dot = 1;
      p++;
      oak_lexer_advance_cursor(cur, 1, 1);
    }
    else if ((c == 'e' || c == 'E') && !has_exp)
    {
      has_exp = 1;
      p++;
      oak_lexer_advance_cursor(cur, 1, 1);

      if (p < end && (*p == '+' || *p == '-'))
      {
        p++;
        oak_lexer_advance_cursor(cur, 1, 1);
      }

      if (p >= end || *p < '0' || *p > '9')
        return OAK_LEX_NUMBER_SYNTAX;
    }
    else
    {
      break;
    }
  }

  const usize len = (usize)(p - start);
  if (len == 0)
    return OAK_LEX_NO_MATCH;

  static _Thread_local char tls_buffer[64];
  if (len >= sizeof(tls_buffer))
    return OAK_LEX_NUMBER_TOO_LONG;

  memset(tls_buffer, 0, sizeof(tls_buffer));
  memcpy(tls_buffer, start, len);
  tls_buffer[len] = '\0';

  if (has_dot || has_exp)
  {
    float val = 0.0f;
    if (sscanf(tls_buffer, "%f", &val) != 1)
      return OAK_LEX_NUMBER_SYNTAX;

    oak_lexer_save_token(
        ctx->lexer, &sav_cur, OAK_TOKEN_FLOAT, (char*)&val, sizeof(float));
  }
  else
  {
    int val = 0;
    if (sscanf(tls_buffer, "%d", &val) != 1)
      return OAK_LEX_NUMBER_SYNTAX;

    oak_lexer_save_token(
        ctx->lexer, &sav_cur, OAK_TOKEN_INT, (char*)&val, sizeof(int));
  }

  return OAK_LEX_OK;
}

static enum oak_lex_status_t scan_ident(const struct oak_lexer_ctx_t* ctx,
                                        const char* input)
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
  oak_growable_buf_init(&gb, tls, ctx->lexer->allocator);

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

/* ---------- Dispatch ---------- */

typedef enum oak_lex_status_t (*scan_fn_t)(const struct oak_lexer_ctx_t* ctx,
                                           const char* input);

static const scan_fn_t scanners[] = {
  scan_ws, scan_block_comment, scan_op, scan_string, scan_number, scan_ident,
};

static enum oak_lex_status_t try_scan(const struct oak_lexer_ctx_t* ctx,
                                      const char* input)
{
  for (usize i = 0; i < oak_count_of(scanners); ++i)
  {
    const enum oak_lex_status_t r = scanners[i](ctx, input);
    if (r == OAK_LEX_OK)
      return OAK_LEX_OK;
    if (r != OAK_LEX_NO_MATCH)
      return r;
  }

  return OAK_LEX_NO_MATCH;
}

struct oak_lexer_result_t* oak_lexer_tokenize(const char* input,
                                              const usize len,
                                              struct oak_allocator_t* allocator)
{
  if (len > 0 && !input)
    return null;

  struct oak_lexer_result_t* result =
      OAK_ALLOC(allocator, sizeof(struct oak_lexer_result_t));
  if (!result)
    return null;

  struct oak_lexer_cur_t cur = {
    .buf_pos = 0, .pos = 1, .line = 1, .column = 1
  };
  const struct oak_lexer_ctx_t ctx = { .lexer = result,
                                       .cur = &cur,
                                       .input_len = len };
  oak_list_init(&result->tokens);
  oak_arena_init(&result->arena, 0, allocator);
  result->error_count = 0;
  result->allocator = allocator;

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
      result->error_count++;
    }
    else
    {
      oak_log(OAK_LOG_ERROR, "lexer: status %d", (int)step);
      result->error_count++;
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

int oak_lexer_error_count(const struct oak_lexer_result_t* result)
{
  return result ? result->error_count : 0;
}

void oak_lexer_free(struct oak_lexer_result_t* result)
{
  if (!result)
    return;
  struct oak_allocator_t* a = result->allocator;
  oak_arena_free(&result->arena);
  OAK_FREE(a, result);
}
