#pragma once

/** Status of a unit test or test helper (oak_test / AST / token checks). */
enum oak_test_status_t
{
  OAK_TEST_OK = 0,

  OAK_TEST_FAIL = 1,

  OAK_TEST_TOKEN_KIND = 2,
  OAK_TEST_TOKEN_LINE = 3,
  OAK_TEST_TOKEN_COLUMN = 4,
  OAK_TEST_TOKEN_OFFSET = 5,
  OAK_TEST_TOKEN_INT = 6,
  OAK_TEST_TOKEN_FLOAT = 7,
  OAK_TEST_TOKEN_STRING = 8,
  OAK_TEST_TOKENS_EXTRA = 9,

  OAK_TEST_AST_KIND = 10,
  OAK_TEST_AST_CHILD_COUNT = 11,
  OAK_TEST_AST_TOKEN_STR = 12,
  OAK_TEST_AST_INT_VAL = 13,
};
