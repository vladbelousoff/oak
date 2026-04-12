#pragma once

#include "oak_countof.h"
#include "oak_lexer.h"
#include "oak_test_run.h"
#include "oak_token.h"

#include <math.h>
#include <string.h>

#define OAK_LEX(S) oak_lexer_tokenize((S), strlen(S))

struct oak_expected_token_t
{
  enum oak_token_kind_t kind;
  int line;
  int column;
  int pos;
  union
  {
    char string[64];
    float floating;
    int integer;
  };
};

static enum oak_test_status_t
oak_test_token(const struct oak_token_t* token,
               const struct oak_expected_token_t* expected,
               const size_t index)
{
  if (oak_token_kind(token) != expected->kind)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu]: kind %s != expected %s",
            index,
            oak_token_name(oak_token_kind(token)),
            oak_token_name(expected->kind));
    return OAK_TEST_TOKEN_KIND;
  }
  if (oak_token_line(token) != expected->line)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu] (%s): line %d != expected %d",
            index,
            oak_token_name(oak_token_kind(token)),
            oak_token_line(token),
            expected->line);
    return OAK_TEST_TOKEN_LINE;
  }
  if (oak_token_column(token) != expected->column)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu] (%s): column %d != expected %d",
            index,
            oak_token_name(oak_token_kind(token)),
            oak_token_column(token),
            expected->column);
    return OAK_TEST_TOKEN_COLUMN;
  }
  if (oak_token_pos(token) != expected->pos)
  {
    oak_log(OAK_LOG_ERR,
            "token[%zu] (%s): pos %d != expected %d",
            index,
            oak_token_name(oak_token_kind(token)),
            oak_token_pos(token),
            expected->pos);
    return OAK_TEST_TOKEN_POS;
  }

  if (oak_token_kind(token) == OAK_TOKEN_INT_NUM)
  {
    if (expected->integer != oak_token_as_i32(token))
    {
      oak_log(OAK_LOG_ERR,
              "token[%zu]: int value %d != expected %d",
              index,
              oak_token_as_i32(token),
              expected->integer);
      return OAK_TEST_TOKEN_INT;
    }
  }

  if (oak_token_kind(token) == OAK_TOKEN_FLOAT_NUM)
  {
    if (fabsf(expected->floating - oak_token_as_f32(token)) > 0.0001f)
    {
      oak_log(OAK_LOG_ERR,
              "token[%zu]: float value %f != expected %f",
              index,
              (double)oak_token_as_f32(token),
              (double)expected->floating);
      return OAK_TEST_TOKEN_FLOAT;
    }
  }

  if (oak_token_kind(token) == OAK_TOKEN_STRING ||
      oak_token_kind(token) == OAK_TOKEN_IDENT)
  {
    if (strcmp(oak_token_buf(token), expected->string) != 0)
    {
      oak_log(OAK_LOG_ERR,
              "token[%zu]: string \"%s\" != expected \"%s\"",
              index,
              oak_token_buf(token),
              expected->string);
      return OAK_TEST_TOKEN_STRING;
    }
  }

  return OAK_TEST_OK;
}

static enum oak_test_status_t
oak_test_tokens(const struct oak_lexer_result_t* lexer,
                const struct oak_expected_token_t* expected_tokens,
                const size_t count)
{
  size_t token_index;
  struct oak_list_entry_t* token_entry;

  oak_list_for_each_indexed(token_index, token_entry, oak_lexer_tokens(lexer))
  {
    if (token_index >= count)
    {
      oak_log(OAK_LOG_ERR,
              "extra token at index %zu (expected %zu tokens)",
              token_index,
              count);
      return OAK_TEST_TOKENS_EXTRA;
    }

    const struct oak_token_t* token =
        oak_container_of(token_entry, struct oak_token_t, link);
    const struct oak_expected_token_t* expected = &expected_tokens[token_index];

    const enum oak_test_status_t step =
        oak_test_token(token, expected, token_index);
    if (step != OAK_TEST_OK)
      return step;
  }

  return OAK_TEST_OK;
}
