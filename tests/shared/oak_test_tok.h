#pragma once

/**
 * Shared helpers for lexer token tests.
 * Include this in each test_*.c file that checks token output.
 */

#include "oak_common.h"
#include "oak_lex.h"
#include "oak_tok.h"

#include <math.h>
#include <string.h>

typedef struct
{
  oak_tok_type_t type;
  int line;
  int column;
  int pos;
  union
  {
    char str[64];
    float f_val;
    int i_val;
  };
} oak_tok_attr_t;

static oak_result_t oak_test_token(const oak_tok_t* tok,
                                   const oak_tok_attr_t* attr)
{
  if (tok->type != attr->type)
    return OAK_FAILURE;
  if (tok->line != attr->line)
    return OAK_FAILURE;
  if (tok->column != attr->column)
    return OAK_FAILURE;
  if (tok->pos != attr->pos)
    return OAK_FAILURE;

  if (tok->type == OAK_TOK_INT_NUM)
  {
    if (attr->i_val != *(int*)tok->buf)
      return OAK_FAILURE;
  }

  if (tok->type == OAK_TOK_FLOAT_NUM)
  {
    if (fabsf(attr->f_val - *(float*)tok->buf) > 0.0001f)
      return OAK_FAILURE;
  }

  if (tok->type == OAK_TOK_STRING)
  {
    if (strcmp(tok->buf, attr->str) != 0)
      return OAK_FAILURE;
  }

  return OAK_SUCCESS;
}

static oak_result_t oak_test_tokens(const oak_lex_t* lex,
                                    const oak_tok_attr_t* attrs,
                                    const size_t count)
{
  size_t tok_index;
  oak_list_entry_t* tok_entry;

  oak_list_for_each_indexed(tok_index, tok_entry, &lex->tokens)
  {
    if (tok_index >= count)
      return OAK_FAILURE;

    const oak_tok_t* tok = oak_container_of(tok_entry, oak_tok_t, link);
    const oak_tok_attr_t* attr = &attrs[tok_index];

    if (oak_test_token(tok, attr) != OAK_SUCCESS)
      return OAK_FAILURE;
  }

  return OAK_SUCCESS;
}
