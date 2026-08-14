/*
 * The module loader: oak_module_loader_load_program.
 *
 * This is the entry point behind the CLI and the WebAssembly playground, and
 * the only one an embedder uses for multi-file programs. The examples exercise
 * it end to end through the CLI; what is checked here is the C API contract
 * itself -- what it returns, what it leaves in the registry and the result, and
 * that both are released without leaking on the failure paths as well as the
 * successful one.
 *
 * Loading is inherently file-backed, so this suite reads the multi-module
 * example from the source tree through OAK_TEST_EXAMPLES_DIR. It writes
 * nothing.
 */

#include "oak_test_support.h"

#include "oak_module_loader.h"

#include <string.h>

OAK_TEST_SUITE(module_loader);

#ifndef OAK_TEST_EXAMPLES_DIR
#define OAK_TEST_EXAMPLES_DIR "examples"
#endif

#define ENTRY_OK OAK_TEST_EXAMPLES_DIR "/06_modules/06_modules.oak"

/* Load `path`, run every assertion the caller wants through `fn`, then tear
 * down in the documented order. The fixture's tracking allocator fails the test
 * if any of it leaks. */
typedef struct load_fixture load_fixture_t;
struct load_fixture
{
  oak_compile_options_t opts;
  oak_module_registry_t registry;
  oak_module_loader_result_t result;
  int rc;
};

static void load_begin(load_fixture_t* f, oak_allocator_t* a, const char* path)
{
  memset(&f->result, 0, sizeof f->result);
  oak_compile_options_init(&f->opts, a);
  oak_module_registry_init(&f->registry, a);
  f->rc = oak_module_loader_load_program(
      path, &f->opts, &f->registry, &f->result);
}

static void load_end(load_fixture_t* f)
{
  oak_module_registry_free(&f->registry);
  oak_compile_options_free(&f->opts);
}

/* The success path: every reachable module is loaded, compiled, and reachable
 * from the registry by id and by path. */
UTEST_F(module_loader, loads_an_entry_and_its_imports)
{
  load_fixture_t f;
  load_begin(&f, OAK_A, ENTRY_OK);

  EXPECT_EQ(0, f.rc);
  EXPECT_EQ(0, f.result.error_count);
  ASSERT_TRUE(f.result.entry != null);

  /* The entry compiled and is marked as the entry. */
  EXPECT_TRUE(oak_module_chunk(f.result.entry) != null);
  EXPECT_TRUE(oak_module_is_entry(f.result.entry));

  /* Its imports were pulled in too: the example imports analytics.stats and
   * domain.project, so the registry holds more than the entry alone. */
  EXPECT_GT(oak_size(f.registry.modules), (usize)1);

  /* Every module in the registry round-trips through the id lookup, and each
   * one that compiled has a chunk. */
  oak_module_t* const* mods = OAK_DATA(oak_module_t*, f.registry.modules);
  for (usize i = 0; i < oak_size(f.registry.modules); ++i)
  {
    oak_module_t* m = mods[i];
    ASSERT_TRUE(m != null);
    /* oak_module_t is opaque, so compare the handles with EXPECT_TRUE:
     * EXPECT_EQ would have utest try to print an incomplete type. */
    EXPECT_TRUE(m == oak_module_registry_get(&f.registry, oak_module_id(m)));
    EXPECT_TRUE(oak_module_path(m) != null);
    EXPECT_TRUE(m == oak_module_registry_find_by_path(&f.registry,
                                                      oak_module_path(m)));
  }

  load_end(&f);
}

/* Only the entry module is the entry, however many modules get loaded. */
UTEST_F(module_loader, exactly_one_module_is_the_entry)
{
  load_fixture_t f;
  load_begin(&f, OAK_A, ENTRY_OK);
  ASSERT_EQ(0, f.rc);

  int entries = 0;
  oak_module_t* const* mods = OAK_DATA(oak_module_t*, f.registry.modules);
  for (usize i = 0; i < oak_size(f.registry.modules); ++i)
    entries += oak_module_is_entry(mods[i]) ? 1 : 0;
  EXPECT_EQ(1, entries);

  load_end(&f);
}

/* A missing entry file fails with a diagnostic rather than a crash or a
 * silent success, and everything allocated along the way is still released. */
UTEST_F(module_loader, a_missing_entry_file_reports_and_frees_cleanly)
{
  load_fixture_t f;
  load_begin(&f, OAK_A, OAK_TEST_EXAMPLES_DIR "/does_not_exist_xyz.oak");

  EXPECT_EQ(-1, f.rc);
  EXPECT_TRUE(f.result.entry == null);
  ASSERT_GT(f.result.error_count, 0);
  EXPECT_LE(f.result.error_count, OAK_MAX_DIAGNOSTICS);
  /* The message names something; an empty diagnostic would be useless. */
  EXPECT_TRUE(f.result.errors[0].message[0] != '\0');

  load_end(&f);
}

/* A registry that was initialized but never successfully loaded still frees
 * cleanly -- the CLI takes this path whenever a program fails to load. */
UTEST_F(module_loader, an_empty_registry_frees_cleanly)
{
  oak_module_registry_t reg;
  oak_module_registry_init(&reg, OAK_A);
  EXPECT_EQ((usize)0, oak_size(reg.modules));
  EXPECT_TRUE(oak_module_registry_get(&reg, 0) == null);
  EXPECT_TRUE(oak_module_registry_find_by_path(&reg, "nope") == null);
  oak_module_registry_free(&reg);
}

/* Accessors are total: a null module answers rather than dereferencing. */
UTEST_F(module_loader, accessors_tolerate_a_null_module)
{
  EXPECT_TRUE(oak_module_chunk(null) == null);
  EXPECT_TRUE(oak_module_dotted_name(null) == null);
  EXPECT_TRUE(oak_module_path(null) == null);
  EXPECT_EQ(OAK_MODULE_ID_NONE, oak_module_id(null));
  EXPECT_EQ(0, oak_module_is_entry(null));
}
