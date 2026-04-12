#pragma once

enum oak_result_t
{
  OAK_SUCCESS,
  OAK_FAILURE,
};

#define OAK_EOS   '\0'
#define OAK_EOL   '\n'
#define OAK_TAB   '\t'
#define OAK_CR    '\r'
#define OAK_SPACE ' '

#define OAK_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
