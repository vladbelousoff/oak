#pragma once

/**
 * Status values for lexer scanning steps (internal scanners and try_scan).
 * OAK_LEX_OK means a token was produced. OAK_LEX_NO_MATCH means this scanner
 * does not apply — the driver tries the next scanner. Other values indicate
 * failure conditions (invalid input, allocation, etc.).
 */
enum oak_lex_status_t
{
  OAK_LEX_OK = 0,
  OAK_LEX_NO_MATCH = 1,

  OAK_LEX_INVALID_UTF8 = 2,
  OAK_LEX_ALLOC_FAILED = 3,
  OAK_LEX_UNTERMINATED_STRING = 4,
  OAK_LEX_NUMBER_SYNTAX = 5,
  OAK_LEX_NUMBER_TOO_LONG = 6,
};
