/*
 * The plugin host: loading a native package's shared library.
 *
 * A plugin is arbitrary machine code that gets to register types and functions
 * the compiler will trust, so every rejection here matters more than the happy
 * path. The tests load real shared modules that meson builds beside this
 * binary -- there is no way to test dlopen without something to open.
 */

#include "oak_test_support.h"

#include "oak_plugin_host.h"

#include <string.h>

OAK_TEST_SUITE(plugin);

#ifndef OAK_TEST_PLUGIN_PATH
#define OAK_TEST_PLUGIN_PATH ""
#endif
#ifndef OAK_TEST_PLUGIN_BAD_ABI_PATH
#define OAK_TEST_PLUGIN_BAD_ABI_PATH ""
#endif

UTEST_F(plugin, loads_a_well_formed_plugin)
{
  oak_plugin_lib_t lib;
  char err[512] = { 0 };

  ASSERT_EQ(0, oak_plugin_host_load(OAK_TEST_PLUGIN_PATH, "test/plugin", &lib,
                                    err, sizeof err));
  ASSERT_TRUE(lib.plugin != OAK_NULL);
  ASSERT_STREQ("test/plugin", lib.plugin->name);
  ASSERT_STREQ("1.0.0", lib.plugin->version);
  ASSERT_TRUE(lib.plugin->bind != OAK_NULL);

  oak_plugin_host_unload(&lib);
  ASSERT_TRUE(lib.handle == OAK_NULL);
}

/* The package name is checked so that a mis-published artifact is caught at
 * load rather than by whatever it registers under. */
UTEST_F(plugin, refuses_a_plugin_belonging_to_another_package)
{
  oak_plugin_lib_t lib;
  char err[512] = { 0 };

  ASSERT_EQ(-1, oak_plugin_host_load(OAK_TEST_PLUGIN_PATH, "acme/something",
                                     &lib, err, sizeof err));
  ASSERT_TRUE(strstr(err, "test/plugin") != OAK_NULL);
  ASSERT_TRUE(lib.handle == OAK_NULL);
}

UTEST_F(plugin, refuses_a_plugin_built_for_another_abi)
{
  oak_plugin_lib_t lib;
  char err[512] = { 0 };

  ASSERT_EQ(-1, oak_plugin_host_load(OAK_TEST_PLUGIN_BAD_ABI_PATH,
                                     "test/bad-abi", &lib, err, sizeof err));
  ASSERT_TRUE(strstr(err, "ABI") != OAK_NULL);
  ASSERT_TRUE(strstr(err, "rebuilding") != OAK_NULL);
  ASSERT_TRUE(lib.handle == OAK_NULL);
}

UTEST_F(plugin, reports_a_library_that_is_not_there)
{
  oak_plugin_lib_t lib;
  char err[512] = { 0 };

  ASSERT_EQ(-1, oak_plugin_host_load("no-such-library-anywhere", "x/y", &lib,
                                     err, sizeof err));
  ASSERT_TRUE(strstr(err, "no-such-library-anywhere") != OAK_NULL);
  ASSERT_TRUE(lib.handle == OAK_NULL);
}

/* Loading the same library twice and unloading both is what happens when two
 * packages in one graph share a dependency, so it has to be ordinary. */
UTEST_F(plugin, loads_the_same_library_twice)
{
  oak_plugin_lib_t first;
  oak_plugin_lib_t second;
  char err[512] = { 0 };

  ASSERT_EQ(0, oak_plugin_host_load(OAK_TEST_PLUGIN_PATH, OAK_NULL, &first, err,
                                    sizeof err));
  ASSERT_EQ(0, oak_plugin_host_load(OAK_TEST_PLUGIN_PATH, OAK_NULL, &second,
                                    err, sizeof err));

  ASSERT_EQ(0, first.plugin->bind(OAK_NULL));
  ASSERT_EQ(0, second.plugin->bind(OAK_NULL));

  oak_plugin_host_unload(&first);
  oak_plugin_host_unload(&second);
}
