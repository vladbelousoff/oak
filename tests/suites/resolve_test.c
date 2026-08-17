/*
 * Dependency resolution: minimal version selection, over fixture manifests.
 *
 * Fetching is a callback, which is what makes this testable at all -- the stub
 * below maps a tag to a directory in the fixtures tree, so the whole algorithm
 * runs with no network, no git, and no cache. What is checked is the behaviour
 * a project depends on being boring: the same package needed twice resolves to
 * one version, the higher minimum wins, incompatible majors are refused rather
 * than silently double-loaded, and a path dependency contributes its own
 * dependencies without ever appearing in the lock.
 */

#include "oak_test_support.h"

#include "internal/oak_pkg_util.h"

#include "oak_pkg_lock.h"
#include "oak_pkg_manifest.h"
#include "oak_pkg_resolve.h"

#include <string.h>

OAK_TEST_SUITE(resolve);

#ifndef OAK_TEST_FIXTURES_DIR
#define OAK_TEST_FIXTURES_DIR "tests/suites/fixtures"
#endif

#define PKG_DIR OAK_TEST_FIXTURES_DIR "/packages"

/* Stands in for the network: a tag names a fixture directory. Deliberately
 * dumb, so a test failure is a failure of the resolver rather than of an
 * elaborate mock. */
static int stub_fetch(void* ctx,
                      const oak_pkg_source_t* source,
                      oak_pkg_fetched_t* out,
                      char* err,
                      const usize err_cap)
{
  oak_allocator_t* a = (oak_allocator_t*)ctx;
  memset(out, 0, sizeof *out);

  const char* which = OAK_NULL;
  if (strstr(source->location, "json") && source->tag)
  {
    if (strcmp(source->tag, "v1.0.0") == 0)
      which = "json-1.0.0";
    else if (strcmp(source->tag, "v1.2.0") == 0)
      which = "json-1.2.0";
    else if (strcmp(source->tag, "v2.0.0") == 0)
      which = "json-2.0.0";
  }
  else if (strstr(source->location, "utf8"))
  {
    which = "utf8-0.4.0";
  }

  if (!which)
    return oak_pkg_fail(err, err_cap, "nothing published at '%s'",
                        source->tag ? source->tag : "?");

  out->dir = oak_pkg_path_join(a, PKG_DIR, which, OAK_HERE);
  /* A plausible commit: the resolver only ever copies it into the lock. */
  out->rev = oak_pkg_strdup(a, "0123456789abcdef0123456789abcdef01234567",
                            OAK_HERE);
  return (out->dir && out->rev) ? 0 : -1;
}

/* Resolve a fixture project and hand back its lock. */
static int resolve_fixture(oak_allocator_t* a,
                           const char* project,
                           oak_pkg_lock_t* lock,
                           char* err,
                           const usize err_cap)
{
  char* dir = oak_pkg_path_join(a, PKG_DIR, project, OAK_HERE);
  char* manifest = dir ? oak_pkg_path_join(a, dir, "oak.json", OAK_HERE)
                       : OAK_NULL;

  oak_pkg_manifest_t root;
  int rc = manifest ? oak_pkg_manifest_read(&root, a, manifest, err, err_cap)
                    : -1;
  oak_free(a, manifest, OAK_HERE);

  if (rc == 0)
  {
    rc = oak_pkg_resolve(a, &root, dir, stub_fetch, a, lock, err, err_cap);
    oak_pkg_manifest_free(&root);
  }
  oak_free(a, dir, OAK_HERE);
  return rc;
}

static const oak_pkg_lock_entry_t* entry_named(const oak_pkg_lock_t* lock,
                                               const char* name)
{
  const oak_pkg_lock_entry_t* e = OAK_CDATA(oak_pkg_lock_entry_t, lock->entries);
  for (usize i = 0; i < oak_size(lock->entries); ++i)
    if (strcmp(e[i].name, name) == 0)
      return &e[i];
  return OAK_NULL;
}

/* The project asks for json ^1.0.0 and its path dependency asks for ^1.2.0.
 * One version is chosen, and it is the higher of the two minimums. */
UTEST_F(resolve, takes_the_highest_minimum_anyone_asked_for)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_lock_t lock;
  ASSERT_EQ(0, resolve_fixture(OAK_A, "app", &lock, err, sizeof err));

  const oak_pkg_lock_entry_t* json = entry_named(&lock, "acme/json");
  ASSERT_TRUE(json != OAK_NULL);
  ASSERT_EQ(1u, json->version.major);
  ASSERT_EQ(2u, json->version.minor);

  oak_pkg_lock_free(&lock);
}

/* One entry per package name, never one per dependent. */
UTEST_F(resolve, locks_a_package_reached_twice_only_once)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_lock_t lock;
  ASSERT_EQ(0, resolve_fixture(OAK_A, "app", &lock, err, sizeof err));

  int seen = 0;
  const oak_pkg_lock_entry_t* e = OAK_CDATA(oak_pkg_lock_entry_t, lock.entries);
  for (usize i = 0; i < oak_size(lock.entries); ++i)
    if (strcmp(e[i].name, "acme/json") == 0)
      ++seen;
  ASSERT_EQ(1, seen);

  oak_pkg_lock_free(&lock);
}

/* The chosen json 1.2.0 brings utf8 with it, so a transitive dependency is
 * part of the lock even though no manifest the project wrote mentions it. */
UTEST_F(resolve, follows_a_dependency_of_a_dependency)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_lock_t lock;
  ASSERT_EQ(0, resolve_fixture(OAK_A, "app", &lock, err, sizeof err));

  ASSERT_TRUE(entry_named(&lock, "acme/utf8") != OAK_NULL);

  oak_pkg_lock_free(&lock);
}

/* A path dependency is walked -- its own dependencies are how json ^1.2.0
 * entered the graph at all -- but it is whatever is on disk, so pinning it
 * would be recording a fact that is not true tomorrow. */
UTEST_F(resolve, never_locks_a_path_dependency)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_lock_t lock;
  ASSERT_EQ(0, resolve_fixture(OAK_A, "app", &lock, err, sizeof err));

  ASSERT_TRUE(entry_named(&lock, "me/locallib") == OAK_NULL);

  oak_pkg_lock_free(&lock);
}

UTEST_F(resolve, records_the_commit_the_fetch_resolved)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_lock_t lock;
  ASSERT_EQ(0, resolve_fixture(OAK_A, "app", &lock, err, sizeof err));

  const oak_pkg_lock_entry_t* json = entry_named(&lock, "acme/json");
  ASSERT_TRUE(json != OAK_NULL);
  ASSERT_EQ(OAK_PKG_SOURCE_GIT, json->kind);
  ASSERT_STREQ("0123456789abcdef0123456789abcdef01234567", json->rev);

  oak_pkg_lock_free(&lock);
}

/* Sorted by name, so a regenerated lock diffs against the old one meaningfully
 * instead of reflecting the order the walk happened to take. */
UTEST_F(resolve, writes_entries_in_name_order)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_lock_t lock;
  ASSERT_EQ(0, resolve_fixture(OAK_A, "app", &lock, err, sizeof err));

  const oak_pkg_lock_entry_t* e = OAK_CDATA(oak_pkg_lock_entry_t, lock.entries);
  for (usize i = 1; i < oak_size(lock.entries); ++i)
    ASSERT_LT(strcmp(e[i - 1u].name, e[i].name), 0);

  oak_pkg_lock_free(&lock);
}

/* A constraint the published version does not satisfy is reported against the
 * dependency, not discovered later as a missing export. */
UTEST_F(resolve, refuses_a_version_the_constraint_excludes)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_manifest_t root;

  static const char manifest[] =
      "{ \"name\": \"me/x\", \"version\": \"1.0.0\", \"deps\": {"
      "  \"json\": { \"git\": \"https://example.test/json.git\","
      "              \"tag\": \"v1.0.0\", \"version\": \"^1.2.0\" } } }";
  ASSERT_EQ(0, oak_pkg_manifest_parse(&root, OAK_A, manifest,
                                      sizeof manifest - 1u, err, sizeof err));

  oak_pkg_lock_t lock;
  ASSERT_EQ(-1, oak_pkg_resolve(OAK_A, &root, PKG_DIR, stub_fetch, OAK_A, &lock,
                                err, sizeof err));
  ASSERT_TRUE(strstr(err, "1.0.0") != OAK_NULL);

  oak_pkg_manifest_free(&root);
}

/* Two majors of one package are two incompatible sets of types wearing the
 * same names, so this is refused rather than resolved to either. */
UTEST_F(resolve, refuses_two_incompatible_majors)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_manifest_t root;

  static const char manifest[] =
      "{ \"name\": \"me/x\", \"version\": \"1.0.0\", \"deps\": {"
      "  \"a\": { \"git\": \"https://example.test/json.git\","
      "           \"tag\": \"v1.2.0\" },"
      "  \"b\": { \"git\": \"https://example.test/json.git\","
      "           \"tag\": \"v2.0.0\" } } }";
  ASSERT_EQ(0, oak_pkg_manifest_parse(&root, OAK_A, manifest,
                                      sizeof manifest - 1u, err, sizeof err));

  oak_pkg_lock_t lock;
  ASSERT_EQ(-1, oak_pkg_resolve(OAK_A, &root, PKG_DIR, stub_fetch, OAK_A, &lock,
                                err, sizeof err));
  ASSERT_TRUE(strstr(err, "not compatible") != OAK_NULL);

  oak_pkg_manifest_free(&root);
}

/* A project with nothing to fetch still resolves, to an empty lock. */
UTEST_F(resolve, resolves_a_project_with_no_dependencies)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_lock_t lock;
  ASSERT_EQ(0, resolve_fixture(OAK_A, "json-1.0.0", &lock, err, sizeof err));
  ASSERT_EQ(0u, oak_size(lock.entries));
  oak_pkg_lock_free(&lock);
}

/* A path dependency that is not there names the path, rather than failing as a
 * missing manifest somewhere unhelpful. */
UTEST_F(resolve, reports_a_missing_path_dependency)
{
  char err[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_manifest_t root;

  static const char manifest[] =
      "{ \"name\": \"me/x\", \"version\": \"1.0.0\", \"deps\": {"
      "  \"gone\": { \"path\": \"../nowhere-at-all\" } } }";
  ASSERT_EQ(0, oak_pkg_manifest_parse(&root, OAK_A, manifest,
                                      sizeof manifest - 1u, err, sizeof err));

  oak_pkg_lock_t lock;
  ASSERT_EQ(-1, oak_pkg_resolve(OAK_A, &root, PKG_DIR, stub_fetch, OAK_A, &lock,
                                err, sizeof err));
  ASSERT_TRUE(strstr(err, "nowhere-at-all") != OAK_NULL);

  oak_pkg_manifest_free(&root);
}
