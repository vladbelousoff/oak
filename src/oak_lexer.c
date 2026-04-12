#include "oak_lexer.h"

#include "oak_arena.h"
#include "oak_countof.h"
#include "oak_lexer_status.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_token.h"
#include "oak_utf8.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
  size_t input_len;
};

static void
advance_cursor(struct oak_lexer_cur_t* cur, const int n, const int bytes)
{
  cur->buf_pos += bytes;
  cur->column += n;
  cur->pos += n;
}

static void new_line(struct oak_lexer_cur_t* cur)
{
  cur->line++;
  cur->column = 0;
}

static void save_token(struct oak_lexer_result_t* lexer,
                       const struct oak_lexer_cur_t* cur,
                       const enum oak_token_kind_t token_kind,
                       const char* buffer,
                       const size_t buffer_size)
{
  size_t token_size = sizeof(struct oak_token_t);
  if (buffer_size > 0)
  {
    token_size += buffer_size + 1;
  }

  struct oak_token_t* token = oak_arena_alloc(&lexer->arena, token_size);
  token->kind = token_kind;
  token->line = cur->line;
  token->column = cur->column;
  token->pos = cur->pos;
  token->size = (int)buffer_size;

  if (buffer && buffer_size > 0)
  {
    memcpy(token->buf, buffer, buffer_size);
  }

  if (buffer_size > 0)
  {
    token->buf[buffer_size] = '\0';
  }

#if 0
  oak_log_cond(token_kind == OAK_TOKEN_IDENT,
               OAK_LOG_DBG,
               "%s %d:%d '%s'",
               oak_token_name(token_kind),
               token->line,
               token->column,
               buffer);

  oak_log_cond(token_kind == OAK_TOKEN_INT_NUM,
               OAK_LOG_DBG,
               "%s %d:%d %d",
               oak_token_name(token_kind),
               token->line,
               token->column,
               *(int*)buffer);

  oak_log_cond(token_kind == OAK_TOKEN_FLOAT_NUM,
               OAK_LOG_DBG,
               "%s %d:%d %f",
               oak_token_name(token_kind),
               token->line,
               token->column,
               *(float*)buffer);

  oak_log_cond(token_kind != OAK_TOKEN_IDENT && token_kind != OAK_TOKEN_INT_NUM &&
                   token_kind != OAK_TOKEN_FLOAT_NUM,
               OAK_LOG_DBG,
               "%s %d:%d",
               oak_token_name(token_kind),
               token->line,
               token->column);
#endif

  oak_list_add_tail(&lexer->tokens, &token->link);
}

static enum oak_lex_status_t try_scan_ws(const struct oak_lexer_ctx_t* ctx,
                                         const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  for (;;)
  {
    if ((size_t)cur->buf_pos >= ctx->input_len)
      break;

    const size_t rem = ctx->input_len - (size_t)cur->buf_pos;
    uint32_t cp = 0;
    const int n = oak_utf8_next_bounded(&input[cur->buf_pos], rem, &cp);

    if (n < 0)
      break;
    if (n == 0)
      break;

    if (cp == ' ' || cp == '\t' || cp == '\r')
    {
      advance_cursor(cur, 1, n);
      continue;
    }

    if (cp == '\n')
    {
      new_line(cur);
      advance_cursor(cur, 1, n);
      continue;
    }

    break;
  }

  return OAK_LEX_NO_MATCH;
}

struct oak_two_char_op_t
{
  char a, b;
  enum oak_token_kind_t token;
};

struct oak_single_char_op_t
{
  char c;
  enum oak_token_kind_t token;
};

/* Two-character operators table */
static const struct oak_two_char_op_t two_char_ops[] = {
  { '=', '=', OAK_TOKEN_EQUAL },        { '!', '=', OAK_TOKEN_NOT_EQUAL },
  { '-', '>', OAK_TOKEN_ARROW },        { '&', '&', OAK_TOKEN_AND },
  { '|', '|', OAK_TOKEN_OR },           { '>', '=', OAK_TOKEN_GREATER_EQUAL },
  { '<', '=', OAK_TOKEN_LESS_EQUAL },   { '+', '=', OAK_TOKEN_PLUS_ASSIGN },
  { '-', '=', OAK_TOKEN_MINUS_ASSIGN }, { '*', '=', OAK_TOKEN_STAR_ASSIGN },
  { '/', '=', OAK_TOKEN_SLASH_ASSIGN }, { '%', '=', OAK_TOKEN_PERCENT_ASSIGN },
};

/* Single-character operators table */
static const struct oak_single_char_op_t single_char_ops[] = {
  { ':', OAK_TOKEN_COLON },
  { ',', OAK_TOKEN_COMMA },
  { ';', OAK_TOKEN_SEMICOLON },
  { '=', OAK_TOKEN_ASSIGN },
  { '!', OAK_TOKEN_EXCLAMATION_MARK },
  { '-', OAK_TOKEN_MINUS },
  { '+', OAK_TOKEN_PLUS },
  { '*', OAK_TOKEN_STAR },
  { '/', OAK_TOKEN_SLASH },
  { '%', OAK_TOKEN_PERCENT },
  { '(', OAK_TOKEN_LPAREN },
  { ')', OAK_TOKEN_RPAREN },
  { '{', OAK_TOKEN_LBRACE },
  { '}', OAK_TOKEN_RBRACE },
  { '[', OAK_TOKEN_LBRACKET },
  { ']', OAK_TOKEN_RBRACKET },
  { '>', OAK_TOKEN_GREATER },
  { '<', OAK_TOKEN_LESS },
  { '.', OAK_TOKEN_DOT },
  { '?', OAK_TOKEN_QUESTION_MARK },
};

static enum oak_lex_status_t try_scan_op(const struct oak_lexer_ctx_t* ctx,
                                         const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  if ((size_t)cur->buf_pos >= ctx->input_len)
    return OAK_LEX_NO_MATCH;

  const char* p = &input[cur->buf_pos];
  const char c1 = p[0];
  const char c2 = (size_t)cur->buf_pos + 1 < ctx->input_len ? p[1] : '\0';
  size_t i;

  /* Save start positions */
  const struct oak_lexer_cur_t sav_cur = *cur;

  /* Check two-character operators first */
  for (i = 0; i < sizeof(two_char_ops) / sizeof(two_char_ops[0]); ++i)
  {
    if (c1 == two_char_ops[i].a && c2 == two_char_ops[i].b)
    {
      const enum oak_token_kind_t token = two_char_ops[i].token;
      save_token(ctx->lexer, &sav_cur, token, p, 0);
      advance_cursor(cur, 2, 2);
      return OAK_LEX_OK;
    }
  }

  /* Check single-character operators */
  for (i = 0; i < oak_countof(single_char_ops); ++i)
  {
    if (c1 == single_char_ops[i].c)
    {
      const enum oak_token_kind_t token = single_char_ops[i].token;
      save_token(ctx->lexer, &sav_cur, token, p, 0);
      advance_cursor(cur, 1, 1);
      return OAK_LEX_OK;
    }
  }

  return OAK_LEX_NO_MATCH;
}

static enum oak_lex_status_t try_scan_string(const struct oak_lexer_ctx_t* ctx,
                                             const char* input)
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
  advance_cursor(cur, 1, 1);

  /* Thread-local static buffer first */
  static _Thread_local char tls_buffer[64];
  char* buffer = tls_buffer;
  size_t buffer_capacity = sizeof(tls_buffer);
  size_t buffer_length = 0;
  int dynamic_alloc = 0;

  const char* const end = input + ctx->input_len;
  const char* p = start + 1;
  while (p < end)
  {
    uint32_t cp;
    int n = oak_utf8_next_bounded(p, (size_t)(end - p), &cp);
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
      advance_cursor(cur, 1, n);

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
          cp = (uint8_t)*p;
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
    advance_cursor(cur, 1, n);

    /* Closing quote found */
    if (*p == '\'')
    {
      advance_cursor(cur, 1, 1);
      save_token(ctx->lexer, &sav_cur, OAK_TOKEN_STRING, buffer, buffer_length);

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

static enum oak_lex_status_t try_scan_number(const struct oak_lexer_ctx_t* ctx,
                                             const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  /* Save start positions */
  const struct oak_lexer_cur_t sav_cur = *cur;

  const char* const end = input + ctx->input_len;
  const char* p = start;
  int has_dot = 0;
  int has_exp = 0;

  // At least one digit
  if (p >= end || *p < '0' || *p > '9')
    return OAK_LEX_NO_MATCH;

  while (p < end)
  {
    const char c = *p;

    if (c >= '0' && c <= '9')
    {
      p++;
      advance_cursor(cur, 1, 1);
    }
    else if (c == '.' && !has_dot && !has_exp)
    {
      has_dot = 1;
      p++;
      advance_cursor(cur, 1, 1);
    }
    else if ((c == 'e' || c == 'E') && !has_exp)
    {
      has_exp = 1;
      p++;
      advance_cursor(cur, 1, 1);

      // Must have at least one digit after e/E
      if (p >= end || *p < '0' || *p > '9')
        return OAK_LEX_NUMBER_SYNTAX;
    }
    else
    {
      break;
    }
  }

  const size_t len = (size_t)(p - start);
  if (len == 0)
    return OAK_LEX_NO_MATCH;

  /* Thread-local buffer to store number as string */
  static _Thread_local char tls_buffer[64];
  if (len >= sizeof(tls_buffer))
    return OAK_LEX_NUMBER_TOO_LONG;

  memset(tls_buffer, 0, sizeof(tls_buffer));
  memcpy(tls_buffer, start, len);
  tls_buffer[len] = '\0';

  if (has_dot || has_exp)
  {
    /* Floating-point */
    float val = 0.0f;
    if (sscanf(tls_buffer, "%f", &val) != 1)
      return OAK_LEX_NUMBER_SYNTAX;

    save_token(
        ctx->lexer, &sav_cur, OAK_TOKEN_FLOAT_NUM, (char*)&val, sizeof(float));
  }
  else
  {
    /* Integer */
    int val = 0;
    if (sscanf(tls_buffer, "%d", &val) != 1)
      return OAK_LEX_NUMBER_SYNTAX;

    save_token(
        ctx->lexer, &sav_cur, OAK_TOKEN_INT_NUM, (char*)&val, sizeof(int));
  }

  return OAK_LEX_OK;
}

static enum oak_lex_status_t try_scan_ident(const struct oak_lexer_ctx_t* ctx,
                                            const char* input)
{
  struct oak_lexer_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  /* Save start positions */
  const struct oak_lexer_cur_t sav_cur = *cur;

  const char* const end = input + ctx->input_len;
  if (start >= end)
    return OAK_LEX_NO_MATCH;

  uint32_t cp = 0;
  int n = oak_utf8_next_bounded(start, (size_t)(end - start), &cp);
  if (n <= 0)
    return OAK_LEX_NO_MATCH;

  /* Identifier must start with a letter or underscore */
  if (!(oak_utf8_is_alpha(cp) || cp == '_'))
    return OAK_LEX_NO_MATCH;

  const char* p = start;

  /* Thread-local static buffer first */
  static _Thread_local char tls_buffer[64];
  char* buffer = tls_buffer;
  size_t buffer_capacity = sizeof(tls_buffer);
  size_t buffer_length = 0;
  memset(buffer, 0, buffer_capacity);
  int dynamic_alloc = 0;

  while (p < end)
  {
    n = oak_utf8_next_bounded(p, (size_t)(end - p), &cp);
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
    advance_cursor(cur, 1, n);
  }

  if (buffer_length == 0)
  {
    if (dynamic_alloc)
      oak_free(buffer, OAK_SRC_LOC);
    return OAK_LEX_NO_MATCH;
  }

  const enum oak_token_kind_t kind = oak_ident_kind(buffer, buffer_length);
  save_token(ctx->lexer,
             &sav_cur,
             kind,
             buffer,
             kind == OAK_TOKEN_IDENT ? buffer_length : 0);

  if (dynamic_alloc)
    oak_free(buffer, OAK_SRC_LOC);

  return OAK_LEX_OK;
}

static struct
{
  enum oak_lex_status_t (*try_scan)(const struct oak_lexer_ctx_t* ctx,
                                    const char* input);
} scanners[] = {
  try_scan_ws,     try_scan_op,    try_scan_string,
  try_scan_number, try_scan_ident, { NULL },
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
                                              const size_t len)
{
  if (len > 0 && !input)
    return NULL;

  struct oak_lexer_result_t* result =
      oak_alloc(sizeof(struct oak_lexer_result_t), OAK_SRC_LOC);
  if (!result)
    return NULL;

  struct oak_lexer_cur_t cur = {
    .buf_pos = 0, .pos = 1, .line = 1, .column = 1
  };
  const struct oak_lexer_ctx_t ctx = {
    .lexer = result, .cur = &cur, .input_len = len
  };
  oak_list_init(&result->tokens);
  oak_arena_init(&result->arena, 0);

  while ((size_t)cur.buf_pos < len)
  {
    if (input[cur.buf_pos] == '\0')
      break;

    const enum oak_lex_status_t step = try_scan(&ctx, input);
    if (step == OAK_LEX_OK)
      continue;

    if (step == OAK_LEX_NO_MATCH)
    {
      uint32_t cp = 0;
      const size_t rem = len - (size_t)cur.buf_pos;
      const int n = oak_utf8_next_bounded(&input[cur.buf_pos], rem, &cp);
      oak_log_cond(n < 0,
                   OAK_LOG_ERR,
                   "invalid utf8 character: 0x%.8X",
                   cp);
    }
    else
    {
      oak_log(OAK_LOG_ERR, "lexer: status %d", (int)step);
      break;
    }
  }

  return result;
}

const struct oak_list_entry_t*
oak_lexer_tokens(const struct oak_lexer_result_t* result)
{
  return result ? &result->tokens : NULL;
}

void oak_lexer_cleanup(struct oak_lexer_result_t* result)
{
  if (!result)
    return;
  oak_arena_destroy(&result->arena);
  oak_free(result, OAK_SRC_LOC);
}
