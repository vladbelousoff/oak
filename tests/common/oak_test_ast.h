#pragma once

#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_test_run.h"

#include <string.h>

#define OAK_LEX(S) oak_lexer_tokenize((S), strlen(S))

static struct oak_ast_node_t*
oak_test_ast_child(const struct oak_ast_node_t* node, const usize index)
{
  return oak_ast_node_child_at(node, index);
}

static usize oak_test_ast_child_count(const struct oak_ast_node_t* node)
{
  return oak_ast_node_child_count(node);
}

static enum oak_test_status_t
oak_test_ast_kind(const struct oak_ast_node_t* node,
                  const enum oak_node_kind_t expected)
{
  if (!node)
    return OAK_TEST_AST_KIND;
  if (node->kind != expected)
    return OAK_TEST_AST_KIND;
  return OAK_TEST_OK;
}

#define OAK_CHECK_NODE_KIND(node, expected)                                    \
  do                                                                           \
  {                                                                            \
    if (oak_test_ast_kind((node), (expected)) != OAK_TEST_OK)                  \
    {                                                                          \
      oak_log(OAK_LOG_ERROR,                                                   \
              "check failed: node kind != %s (%s:%d)",                         \
              #expected,                                                       \
              oak_path_basename(__FILE__),                                     \
              __LINE__);                                                       \
      return OAK_TEST_AST_KIND;                                                \
    }                                                                          \
  } while (0)

#define OAK_CHECK_CHILD_COUNT(node, expected)                                  \
  do                                                                           \
  {                                                                            \
    const usize _count = oak_test_ast_child_count(node);                       \
    if (_count != (usize)(expected))                                           \
    {                                                                          \
      oak_log(OAK_LOG_ERROR,                                                   \
              "check failed: child count %zu != %d (%s:%d)",                   \
              _count,                                                          \
              (int)(expected),                                                 \
              oak_path_basename(__FILE__),                                     \
              __LINE__);                                                       \
      return OAK_TEST_AST_CHILD_COUNT;                                         \
    }                                                                          \
  } while (0)

#define OAK_CHECK_TOKEN_STR(node, expected)                                    \
  do                                                                           \
  {                                                                            \
    if (strcmp(oak_token_text((node)->token), (expected)) != 0)                \
    {                                                                          \
      oak_log(OAK_LOG_ERROR,                                                   \
              "check failed: token \"%s\" != \"%s\" (%s:%d)",                  \
              oak_token_text((node)->token),                                   \
              (expected),                                                      \
              oak_path_basename(__FILE__),                                     \
              __LINE__);                                                       \
      return OAK_TEST_AST_TOKEN_STR;                                           \
    }                                                                          \
  } while (0)

#define OAK_CHECK_INT_VAL(node, expected)                                      \
  do                                                                           \
  {                                                                            \
    const int _val = oak_token_as_i32((node)->token);                          \
    if (_val != (expected))                                                    \
    {                                                                          \
      oak_log(OAK_LOG_ERROR,                                                   \
              "check failed: int value %d != %d (%s:%d)",                      \
              _val,                                                            \
              (int)(expected),                                                 \
              oak_path_basename(__FILE__),                                     \
              __LINE__);                                                       \
      return OAK_TEST_AST_INT_VAL;                                             \
    }                                                                          \
  } while (0)
