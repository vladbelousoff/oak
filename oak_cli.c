#include "oak_cli.h"

#include <string.h>

static int is_long_option(const char* s)
{
  return s && s[0] == '-' && s[1] == '-' && s[2] != '\0';
}

int oak_cli_parse(int argc,
                  const char* const* argv,
                  struct oak_cli_args_t* args)
{
  memset(args, 0, sizeof(*args));

  int i = 1;

  /* Parse oak flags before the script name */
  for (; i < argc; ++i)
  {
    const char* a = argv[i];

    if (strcmp(a, "--") == 0)
    {
      ++i;
      break;
    }

    if (a[0] == '-' && a[1] != '\0' && a[1] != '-')
    {
      args->error = "short options are not supported; use --long-option";
      return -1;
    }

    if (is_long_option(a))
    {
      if (strchr(a + 2, '='))
      {
        args->error = "option values are not supported";
        return -1;
      }
      if (strcmp(a, "--help") == 0)
      {
        args->help = 1;
        continue;
      }
      if (strcmp(a, "--disassemble") == 0)
      {
        args->disassemble = 1;
        continue;
      }
      if (strcmp(a, "--no-debug") == 0)
      {
        args->no_debug = 1;
        continue;
      }
      args->error = "unknown option";
      return -1;
    }

    /* First non-option is the script path */
    args->script_path = a;
    ++i;
    break;
  }

  /* Everything after the script name is passed to the script */
  if (args->script_path && i < argc)
  {
    args->script_argv = argv + i;
    args->script_argc = argc - i;
  }

  if (args->help)
    return 0;

  if (!args->script_path)
  {
    args->error = "no script path";
    return -1;
  }

  return 0;
}

void oak_cli_usage(FILE* out)
{
  fprintf(out,
          "usage: oak [--disassemble] [--no-debug] [--help] <script> [script "
          "args...]\n");
}
