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

  const char* debug_port[] = {
    "oak", "--debug", "--debug-port", "0", "main.oak"
  };
  CHECK(oak_cli_parse(5, debug_port, &args) == 0);
  CHECK(args.debug);
  CHECK(args.debug_port == 0);

  const char* synthetic[] = { "oak", "--allow-synthetic-modules", "main.oak" };
  CHECK(oak_cli_parse(3, synthetic, &args) == 0);
  CHECK(args.allow_synthetic_modules);
  CHECK(strcmp(args.script_path, "main.oak") == 0);

  const char* no_synthetic[] = { "oak", "main.oak" };
  CHECK(oak_cli_parse(2, no_synthetic, &args) == 0);
  CHECK(!args.allow_synthetic_modules);

  const char* invalid_port[] = { "oak", "--debug-port", "70000", "main.oak" };
  CHECK(oak_cli_parse(4, invalid_port, &args) == -1);
  CHECK(strcmp(args.error, "invalid debug port") == 0);

  return 0;
}
