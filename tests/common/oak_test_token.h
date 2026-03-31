#pragma once

#include "oak_lexer.h"
#include "oak_test_run.h"
#include "oak_token.h"

#include <math.h>
#include <string.h>

typedef struct
{
  oak_token_kind_t kind;
  int line;
  int column;
  int pos;
  union
  {
    char str[64];
    float f_val;
    int i_val;
  };
} oak_token_attr_t;

static oak_result_t oak_test_token(const oak_token_t* token,
                                   const oak_token_attr_t* attr,
                                   const size_t index)
{
  if (token->kind != attr->kind)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu]: kind %s != expected %s",
            index,
            oak_token_name(token->kind),
            oak_token_name(attr->kind));
    return OAK_FAILURE;
  }
  if (token->line != attr->line)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu] (%s): line %d != expected %d",
            index,
            oak_token_name(token->kind),
            token->line,
            attr->line);
    return OAK_FAILURE;
  }
  if (token->column != attr->column)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu] (%s): column %d != expected %d",
            index,
            oak_token_name(token->kind),
            token->column,
            attr->column);
    return OAK_FAILURE;
  }
  if (token->pos != attr->pos)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu] (%s): pos %d != expected %d",
            index,
            oak_token_name(token->kind),
            token->pos,
            attr->pos);
    return OAK_FAILURE;
  }

  if (token->kind == OAK_TOKEN_INT_NUM)
  {
    if (attr->i_val != *(int*)token->buf)
    {
      oak_log(OAK_LOG_ERR,
              "token[%zu]: int value %d != expected %d",
              index,
              *(int*)token->buf,
              attr->i_val);
      return OAK_FAILURE;
    }
  }

  if (token->kind == OAK_TOKEN_FLOAT_NUM)
  {
    if (fabsf(attr->f_val - *(float*)token->buf) > 0.0001f)
    {
      oak_log(OAK_LOG_ERR,
              "token[%zu]: float value %f != expected %f",
              index,
              (double)*(float*)token->buf,
              (double)attr->f_val);
      return OAK_FAILURE;
    }
  }

  if (token->kind == OAK_TOKEN_STRING || token->kind == OAK_TOKEN_IDENT)
  {
    if (strcmp(token->buf, attr->str) != 0)
    {
      oak_log(OAK_LOG_ERR,
              "token[%zu]: string \"%s\" != expected \"%s\"",
              index,
              token->buf,
              attr->str);
      return OAK_FAILURE;
    }
  }

  return OAK_SUCCESS;
}

static oak_result_t oak_test_tokens(const oak_lexer_result_t* lexer,
                                    const oak_token_attr_t* attrs,
                                    const size_t count)
{
  size_t token_index;
  oak_list_entry_t* token_entry;

  oak_list_for_each_indexed(token_index, token_entry, oak_lexer_tokens(lexer))
  {
    if (token_index >= count)
    {
      oak_log(OAK_LOG_ERR,
              "extra token at index %zu (expected %zu tokens)",
              token_index,
              count);
      return OAK_FAILURE;
    }

    const oak_token_t* token = oak_container_of(token_entry, oak_token_t, link);
    const oak_token_attr_t* attr = &attrs[token_index];

    if (oak_test_token(token, attr, token_index) != OAK_SUCCESS)
      return OAK_FAILURE;
  }

  return OAK_SUCCESS;
}
