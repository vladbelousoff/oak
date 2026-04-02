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
    char string[64];
    float floating;
    int integer;
  };
} oak_expected_token_t;

static oak_result_t oak_test_token(const oak_token_t* token,
                                   const oak_expected_token_t* expected,
                                   const size_t index)
{
  if (token->kind != expected->kind)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu]: kind %s != expected %s",
            index,
            oak_token_name(token->kind),
            oak_token_name(expected->kind));
    return OAK_FAILURE;
  }
  if (token->line != expected->line)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu] (%s): line %d != expected %d",
            index,
            oak_token_name(token->kind),
            token->line,
            expected->line);
    return OAK_FAILURE;
  }
  if (token->column != expected->column)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu] (%s): column %d != expected %d",
            index,
            oak_token_name(token->kind),
            token->column,
            expected->column);
    return OAK_FAILURE;
  }
  if (token->pos != expected->pos)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu] (%s): pos %d != expected %d",
            index,
            oak_token_name(token->kind),
            token->pos,
            expected->pos);
    return OAK_FAILURE;
  }

  if (token->kind == OAK_TOKEN_INT_NUM)
  {
    if (expected->integer != *(int*)token->buf)
    {
      oak_log(OAK_LOG_ERR,
              "token[%zu]: int value %d != expected %d",
              index,
              *(int*)token->buf,
              expected->integer);
      return OAK_FAILURE;
    }
  }

  if (token->kind == OAK_TOKEN_FLOAT_NUM)
  {
    if (fabsf(expected->floating - *(float*)token->buf) > 0.0001f)
    {
      oak_log(OAK_LOG_ERR,
              "token[%zu]: float value %f != expected %f",
              index,
              (double)*(float*)token->buf,
              (double)expected->floating);
      return OAK_FAILURE;
    }
  }

  if (token->kind == OAK_TOKEN_STRING || token->kind == OAK_TOKEN_IDENT)
  {
    if (strcmp(token->buf, expected->string) != 0)
    {
      oak_log(OAK_LOG_ERR,
              "token[%zu]: string \"%s\" != expected \"%s\"",
              index,
              token->buf,
              expected->string);
      return OAK_FAILURE;
    }
  }

  return OAK_SUCCESS;
}

static oak_result_t oak_test_tokens(const oak_lexer_result_t* lexer,
                                    const oak_expected_token_t* expected_tokens,
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
    const oak_expected_token_t* expected = &expected_tokens[token_index];

    if (oak_test_token(token, expected, token_index) != OAK_SUCCESS)
      return OAK_FAILURE;
  }

  return OAK_SUCCESS;
}
