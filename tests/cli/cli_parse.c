#include "oak_cli.h"

#include <string.h>

#define CHECK(expr)                                                            \
  do                                                                           \
  {                                                                            \
    if (!(expr))                                                               \
      return __LINE__;                                                         \
  } while (0)

int main(void)
{
  struct oak_cli_args_t args;

  const char* no_debug_symbols[] = { "oak", "--no-debug-symbols", "main.oak" };
  CHECK(oak_cli_parse(3, no_debug_symbols, &args) == 0);
  CHECK(args.no_debug_symbols);
  CHECK(strcmp(args.script_path, "main.oak") == 0);

  const char* removed_no_debug[] = { "oak", "--no-debug", "main.oak" };
  CHECK(oak_cli_parse(3, removed_no_debug, &args) == -1);
  CHECK(strcmp(args.error, "unknown option") == 0);

  const char* script_args[] = { "oak", "main.oak", "--no-debug" };
  CHECK(oak_cli_parse(3, script_args, &args) == 0);
  CHECK(!args.no_debug_symbols);
  CHECK(args.script_argc == 1);
  CHECK(strcmp(args.script_argv[0], "--no-debug") == 0);

  return 0;
}
