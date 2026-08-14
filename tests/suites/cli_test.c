/*
 * CLI argument parsing.
 *
 * Pure argument parsing against oak_cli.c -- no allocator, no VM, so these use
 * plain UTEST rather than the leak-checking fixture.
 */

#include "utest.h"

#include "oak_cli.h"

#include <string.h>

UTEST(cli, script_path_and_flags_are_separated)
{
  oak_cli_args_t args;
  static const char* const argv[] = { "oak", "--no-debug-symbols", "main.oak" };

  ASSERT_EQ(0, oak_cli_parse(3, argv, &args));
  EXPECT_TRUE(args.no_debug_symbols);
  EXPECT_STREQ("main.oak", args.script_path);
}

/*
 * Everything after the script path belongs to the script, even when it looks
 * exactly like an oak option. Without this, a script could never take an
 * argument that collides with a CLI flag.
 */
UTEST(cli, arguments_after_the_script_path_go_to_the_script)
{
  oak_cli_args_t args;
  static const char* const argv[] = { "oak", "main.oak", "--no-debug" };

  ASSERT_EQ(0, oak_cli_parse(3, argv, &args));
  EXPECT_FALSE(args.no_debug_symbols);
  ASSERT_EQ(1, args.script_argc);
  EXPECT_STREQ("--no-debug", args.script_argv[0]);
}

/* `--no-debug` was renamed to `--no-debug-symbols`; the old spelling must be
 * an error rather than silently doing nothing. */
UTEST(cli, the_removed_no_debug_flag_is_rejected)
{
  oak_cli_args_t args;
  static const char* const argv[] = { "oak", "--no-debug", "main.oak" };

  ASSERT_EQ(-1, oak_cli_parse(3, argv, &args));
  EXPECT_STREQ("unknown option", args.error);
}

UTEST(cli, debug_options_are_parsed)
{
  oak_cli_args_t args;
  static const char* const argv[] = {
    "oak", "--debug", "--debug-port", "0", "main.oak"
  };

  ASSERT_EQ(0, oak_cli_parse(5, argv, &args));
  EXPECT_TRUE(args.debug);
  EXPECT_EQ(0, args.debug_port);
}

/* A port outside the 16-bit range is rejected rather than truncated. */
UTEST(cli, an_out_of_range_debug_port_is_rejected)
{
  oak_cli_args_t args;
  static const char* const argv[] = {
    "oak", "--debug-port", "70000", "main.oak"
  };

  ASSERT_EQ(-1, oak_cli_parse(4, argv, &args));
  EXPECT_STREQ("invalid debug port", args.error);
}

/* Unbuffered output is opt-in: a normal run keeps stdio's own buffering. */
UTEST(cli, unbuffered_output_is_opt_in)
{
  oak_cli_args_t args;
  static const char* const enabled[] = { "oak", "--unbuffered", "main.oak" };
  static const char* const defaulted[] = { "oak", "main.oak" };

  ASSERT_EQ(0, oak_cli_parse(3, enabled, &args));
  EXPECT_TRUE(args.unbuffered);
  EXPECT_STREQ("main.oak", args.script_path);

  ASSERT_EQ(0, oak_cli_parse(2, defaulted, &args));
  EXPECT_FALSE(args.unbuffered);
}

/* Synthetic native modules are opt-in and off by default. */
UTEST(cli, synthetic_modules_are_opt_in)
{
  oak_cli_args_t args;
  static const char* const enabled[] = {
    "oak", "--allow-synthetic-modules", "main.oak"
  };
  static const char* const defaulted[] = { "oak", "main.oak" };

  ASSERT_EQ(0, oak_cli_parse(3, enabled, &args));
  EXPECT_TRUE(args.allow_synthetic_modules);
  EXPECT_STREQ("main.oak", args.script_path);

  ASSERT_EQ(0, oak_cli_parse(2, defaulted, &args));
  EXPECT_FALSE(args.allow_synthetic_modules);
}
