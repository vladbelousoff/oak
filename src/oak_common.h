#pragma once

typedef enum
{
  OAK_SUCCESS,
  OAK_FAILURE,
} oak_result_t;

#define OAK_EOS   '\0'
#define OAK_EOL   '\n'
#define OAK_TAB   '\t'
#define OAK_CR    '\r'
#define OAK_SPACE ' '

#define OAK_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
