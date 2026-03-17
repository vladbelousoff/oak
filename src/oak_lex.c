#include "oak_lex.h"

#include "oak_common.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_tok.h"
#include "oak_utf8.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
  int buf_pos;
  int pos;
  int line;
  int column;
} oak_lex_cur_t;

typedef struct
{
  oak_lex_t* lex;
  oak_lex_cur_t* cur;
} oak_lex_ctx_t;

static void advance_cursor(oak_lex_cur_t* cur, const int n, const int bytes)
{
  cur->buf_pos += bytes;
  cur->column += n;
  cur->pos += n;
}

static void new_line(oak_lex_cur_t* cur)
{
  cur->line++;
  cur->column = 0;
}

static void save_token(const oak_src_loc_t src_loc,
                       oak_lex_t* lex,
                       const oak_lex_cur_t* cur,
                       const oak_tok_type_t token_type,
                       const char* buffer,
                       const size_t buffer_size)
{
  size_t token_size = sizeof(oak_tok_t);
  if (buffer_size > 0)
  {
    token_size += buffer_size + 1;
  }

  oak_tok_t* token = oak_mem_acquire(src_loc, token_size);
  token->type = token_type;
  token->line = cur->line;
  token->column = cur->column;
  token->pos = cur->pos;
  token->size = buffer_size;

  if (buffer && buffer_size > 0)
  {
    memcpy(token->buf, buffer, buffer_size);
  }

  if (buffer_size > 0)
  {
    token->buf[buffer_size] = OAK_EOS;
  }

#if 0
  oak_log_cond(token_type == OAK_TOK_IDENT,
               OAK_LOG_DBG,
               "%s %d:%d '%s'",
               oak_tok_name(token_type),
               token->line,
               token->column,
               buffer);

  oak_log_cond(token_type == OAK_TOK_INT_NUM,
               OAK_LOG_DBG,
               "%s %d:%d %d",
               oak_tok_name(token_type),
               token->line,
               token->column,
               *(int*)buffer);

  oak_log_cond(token_type == OAK_TOK_FLOAT_NUM,
               OAK_LOG_DBG,
               "%s %d:%d %f",
               oak_tok_name(token_type),
               token->line,
               token->column,
               *(float*)buffer);

  oak_log_cond(token_type != OAK_TOK_IDENT && token_type != OAK_TOK_INT_NUM &&
                   token_type != OAK_TOK_FLOAT_NUM,
               OAK_LOG_DBG,
               "%s %d:%d",
               oak_tok_name(token_type),
               token->line,
               token->column);
#endif

  oak_list_add_tail(&lex->tokens, &token->link);
}

static oak_result_t try_scan_ws(const oak_lex_ctx_t* ctx, const char* input)
{
  oak_lex_cur_t* cur = ctx->cur;
  for (;;)
  {
    uint32_t cp = 0;
    const int n = oak_utf8_next(&input[cur->buf_pos], &cp);

    if (n < 0)
    {
      break;
    }

    if (cp == OAK_EOS)
    {
      break;
    }

    if (cp == OAK_SPACE || cp == OAK_TAB || cp == OAK_CR)
    {
      advance_cursor(cur, 1, n);
      continue;
    }

    if (cp == OAK_EOL)
    {
      new_line(cur);
      advance_cursor(cur, 1, n);
      continue;
    }

    break;
  }

  return OAK_FAILURE;
}

typedef struct
{
  char a, b;
  oak_tok_type_t tok;
} oak_two_char_op_t;

typedef struct
{
  char c;
  oak_tok_type_t tok;
} oak_single_char_op_t;

/* Two-character operators table */
static const oak_two_char_op_t two_char_ops[] = {
  { '=', '=', OAK_TOK_EQUAL },      { '!', '=', OAK_TOK_NOT_EQUAL },
  { '-', '>', OAK_TOK_ARROW },      { '&', '&', OAK_TOK_AND },
  { '|', '|', OAK_TOK_OR },         { '>', '=', OAK_TOK_GREATER_EQUAL },
  { '<', '=', OAK_TOK_LESS_EQUAL },
};

/* Single-character operators table */
static const oak_single_char_op_t single_char_ops[] = {
  { ':', OAK_TOK_COLON },
  { ',', OAK_TOK_COMMA },
  { ';', OAK_TOK_SEMICOLON },
  { '=', OAK_TOK_ASSIGN },
  { '!', OAK_TOK_EXCLAMATION_MARK },
  { '-', OAK_TOK_MINUS },
  { '+', OAK_TOK_PLUS },
  { '*', OAK_TOK_STAR },
  { '/', OAK_TOK_SLASH },
  { '(', OAK_TOK_LPAREN },
  { ')', OAK_TOK_RPAREN },
  { '{', OAK_TOK_LBRACE },
  { '}', OAK_TOK_RBRACE },
  { '[', OAK_TOK_LBRACKET },
  { ']', OAK_TOK_RBRACKET },
  { '>', OAK_TOK_GREATER },
  { '<', OAK_TOK_LESS },
  { '.', OAK_TOK_DOT },
  { '?', OAK_TOK_QUESTION_MARK },
};

static oak_result_t try_scan_op(const oak_lex_ctx_t* ctx, const char* input)
{
  oak_lex_cur_t* cur = ctx->cur;
  const char* p = &input[cur->buf_pos];
  const char c1 = p[0];
  const char c2 = p[1];
  size_t i;

  /* Save start positions */
  const oak_lex_cur_t sav_cur = *cur;

  /* Check two-character operators first */
  for (i = 0; i < OAK_ARRAY_SIZE(two_char_ops); ++i)
  {
    if (c1 == two_char_ops[i].a && c2 == two_char_ops[i].b)
    {
      const oak_tok_type_t tok = two_char_ops[i].tok;
      save_token(OAK_SRC_LOC, ctx->lex, &sav_cur, tok, p, 0);
      advance_cursor(cur, 2, 2);
      return OAK_SUCCESS;
    }
  }

  /* Check single-character operators */
  for (i = 0; i < OAK_ARRAY_SIZE(single_char_ops); ++i)
  {
    if (c1 == single_char_ops[i].c)
    {
      const oak_tok_type_t tok = single_char_ops[i].tok;
      save_token(OAK_SRC_LOC, ctx->lex, &sav_cur, tok, p, 0);
      advance_cursor(cur, 1, 1);
      return OAK_SUCCESS;
    }
  }

  return OAK_FAILURE;
}

static oak_result_t try_scan_string(const oak_lex_ctx_t* ctx, const char* input)
{
  oak_lex_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  /* Not a string literal */
  if (*start != '\'')
  {
    return OAK_FAILURE;
  }

  /* Save start positions */
  const oak_lex_cur_t sav_cur = *cur;

  /* Skip opening quote */
  advance_cursor(cur, 1, 1);

  /* Thread-local static buffer first */
  static _Thread_local char tls_buffer[64];
  char* buffer = tls_buffer;
  size_t buffer_capacity = sizeof(tls_buffer);
  size_t buffer_length = 0;
  int dynamic_alloc = 0;

  const char* p = start + 1;
  while (*p)
  {
    uint32_t cp;
    int n = oak_utf8_next(p, &cp);
    if (n <= 0)
    {
      if (dynamic_alloc)
        oak_mem_release(OAK_SRC_LOC, buffer);
      /* Invalid UTF-8 */
      return OAK_FAILURE;
    }

    /* Handle escape sequences */
    if (cp == '\\')
    {
      p += n;
      advance_cursor(cur, 1, n);

      if (*p == '\0')
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
        buffer_capacity = 128;
        char* new_buf = oak_mem_acquire(OAK_SRC_LOC, buffer_capacity);
        if (!new_buf)
          return OAK_FAILURE;
        memcpy(new_buf, tls_buffer, buffer_length);
        buffer = new_buf;
        dynamic_alloc = 1;
      }
      else
      {
        buffer_capacity *= 2;
        char* new_buf = oak_mem_realloc(OAK_SRC_LOC, buffer, buffer_capacity);
        if (!new_buf)
        {
          oak_mem_release(OAK_SRC_LOC, buffer);
          return OAK_FAILURE;
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
      save_token(OAK_SRC_LOC,
                 ctx->lex,
                 &sav_cur,
                 OAK_TOK_STRING,
                 buffer,
                 buffer_length);

      if (dynamic_alloc)
        oak_mem_release(OAK_SRC_LOC, buffer);
      return OAK_SUCCESS;
    }
  }

  /* Unterminated string literal */
  if (dynamic_alloc)
    oak_mem_release(OAK_SRC_LOC, buffer);
  return OAK_FAILURE;
}

static oak_result_t try_scan_number(const oak_lex_ctx_t* ctx, const char* input)
{
  oak_lex_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  /* Save start positions */
  const oak_lex_cur_t sav_cur = *cur;

  const char* p = start;
  int has_dot = 0;
  int has_exp = 0;

  // At least one digit
  if (*p < '0' || *p > '9')
    return OAK_FAILURE;

  while (*p)
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
      if (*p < '0' || *p > '9')
        return OAK_FAILURE;
    }
    else
    {
      break;
    }
  }

  const size_t len = p - start;
  if (len == 0)
    return OAK_FAILURE;

  /* Thread-local buffer to store number as string */
  static _Thread_local char tls_buffer[64];
  if (len >= sizeof(tls_buffer))
    return OAK_FAILURE;

  memset(tls_buffer, 0, sizeof(tls_buffer));
  memcpy(tls_buffer, start, len);
  tls_buffer[len] = OAK_EOS;

  if (has_dot || has_exp)
  {
    /* Floating-point */
    float val = 0.0f;
    if (sscanf_s(tls_buffer, "%f", &val) != 1)
      return OAK_FAILURE;

    save_token(OAK_SRC_LOC,
               ctx->lex,
               &sav_cur,
               OAK_TOK_FLOAT_NUM,
               (char*)&val,
               sizeof(float));
  }
  else
  {
    /* Integer */
    int val = 0;
    if (sscanf_s(tls_buffer, "%d", &val) != 1)
      return OAK_FAILURE;

    save_token(OAK_SRC_LOC,
               ctx->lex,
               &sav_cur,
               OAK_TOK_INT_NUM,
               (char*)&val,
               sizeof(int));
  }

  return OAK_SUCCESS;
}

static oak_result_t try_scan_ident(const oak_lex_ctx_t* ctx, const char* input)
{
  oak_lex_cur_t* cur = ctx->cur;
  const char* start = &input[cur->buf_pos];

  /* Save start positions */
  const oak_lex_cur_t sav_cur = *cur;

  uint32_t cp = 0;
  int n = oak_utf8_next(start, &cp);
  if (n != 1)
    return OAK_FAILURE;

  /* Identifier must start with a letter or underscore */
  if (!((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_'))
    return OAK_FAILURE;

  const char* p = start;

  /* Thread-local static buffer first */
  static _Thread_local char tls_buffer[64];
  char* buffer = tls_buffer;
  size_t buffer_capacity = sizeof(tls_buffer);
  size_t buffer_length = 0;
  memset(buffer, 0, buffer_capacity);
  int dynamic_alloc = 0;

  while (*p)
  {
    n = oak_utf8_next(p, &cp);
    if (n <= 0)
      break;

    /* Valid identifier characters: letters, digits, underscore */
    if (!((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
          (cp >= '0' && cp <= '9') || cp == '_'))
      break;

    /* Resize buffer if needed */
    if (buffer_length + n >= buffer_capacity)
    {
      if (!dynamic_alloc)
      {
        buffer_capacity = 128;
        char* new_buf = oak_mem_acquire(OAK_SRC_LOC, buffer_capacity);
        memset(new_buf, 0, buffer_capacity);
        if (!new_buf)
          return OAK_FAILURE;
        memcpy(new_buf, tls_buffer, buffer_length);
        buffer = new_buf;
        dynamic_alloc = 1;
      }
      else
      {
        buffer_capacity *= 2;
        char* new_buf = oak_mem_realloc(OAK_SRC_LOC, buffer, buffer_capacity);
        if (!new_buf)
        {
          oak_mem_release(OAK_SRC_LOC, buffer);
          return OAK_FAILURE;
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
      oak_mem_release(OAK_SRC_LOC, buffer);
    return OAK_FAILURE;
  }

  const oak_tok_type_t type = oak_ident_type(buffer, buffer_length);
  save_token(OAK_SRC_LOC,
             ctx->lex,
             &sav_cur,
             type,
             buffer,
             type == OAK_TOK_IDENT ? buffer_length : 0);

  if (dynamic_alloc)
    oak_mem_release(OAK_SRC_LOC, buffer);

  return OAK_SUCCESS;
}

static struct
{
  oak_result_t (*try_scan)(const oak_lex_ctx_t* ctx, const char* input);
} scanners[] = {
  try_scan_ws,     try_scan_op,    try_scan_string,
  try_scan_number, try_scan_ident, { NULL },
};

static oak_result_t try_scan(const oak_lex_ctx_t* ctx, const char* input)
{
  for (int i = 0; scanners[i].try_scan; ++i)
  {
    if (scanners[i].try_scan(ctx, input) != OAK_FAILURE)
    {
      return OAK_SUCCESS;
    }
  }

  return OAK_FAILURE;
}

void oak_lex_tokenize(const char* input, oak_lex_t* output)
{
  oak_lex_cur_t cur = { .buf_pos = 0, .pos = 1, .line = 1, .column = 1 };
  const oak_lex_ctx_t ctx = { .lex = output, .cur = &cur };
  oak_list_init(&output->tokens);

  while (input[cur.buf_pos] != OAK_EOS)
  {
    if (try_scan(&ctx, input) != OAK_SUCCESS)
    {
      uint32_t cp = 0;
      oak_log_cond(oak_utf8_next(&input[cur.buf_pos], &cp) < 0,
                   OAK_LOG_ERR,
                   "Invalid UTF-8 character: 0x%.8X",
                   cp);
    }
  }
}

void oak_lex_cleanup(const oak_lex_t* lex)
{
  oak_list_entry_t* entry;
  oak_list_entry_t* safe;
  oak_list_for_each_safe(entry, safe, &lex->tokens)
  {
    oak_tok_t* token = oak_container_of(entry, oak_tok_t, link);
    oak_list_remove(entry);
    oak_mem_release(OAK_SRC_LOC, token);
  }
}