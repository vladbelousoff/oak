#pragma once

#include <stdio.h>

typedef struct oak_cli_args oak_cli_args_t;
struct oak_cli_args
{
  int allow_synthetic_modules;
  int disassemble;
  int debug;
  int debug_port;
  int debug_port_set;
  int help;
  int no_debug_symbols;
  int no_plugins;
  int track_memory;
  int unbuffered;
  int version;
  const char* script_path;
  const char* const* script_argv;
  int script_argc;
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
                  oak_cli_args_t* args);

void oak_cli_usage(FILE* out);
