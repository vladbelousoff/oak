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

  int after_dd = 0;

  for (int i = 1; i < argc; ++i)
  {
    const char* a = argv[i];

    if (after_dd)
    {
      if (args->script_path)
      {
        args->error = "multiple script paths";
        return -1;
      }
      args->script_path = a;
      continue;
    }

    if (strcmp(a, "--") == 0)
    {
      after_dd = 1;
      continue;
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
      args->error = "unknown option";
      return -1;
    }

    if (args->script_path)
    {
      args->error = "multiple script paths";
      return -1;
    }
    args->script_path = a;
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
          "usage: oak [--disassemble] [--help] <script>\n"
          "       oak <script> [--disassemble]\n");
}
