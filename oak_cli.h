#pragma once

#include <stdio.h>

struct oak_cli_args_t
{
  int disassemble;
  int help;
  const char* script_path;
  const char* error;
};

/**
 * Parse command-line arguments. Only GNU-style long options (--name) are
 * accepted; single-dash short options are rejected.
 *
 * @return 0 on success, -1 on error (see args->error).
 */
int oak_cli_parse(int argc,
                  const char* const* argv,
                  struct oak_cli_args_t* args);

void oak_cli_usage(FILE* out);
