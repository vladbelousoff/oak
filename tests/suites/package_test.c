/*
 * The package layer: semantic versions, dependency specs, and oak.json.
 *
 * All of it is pure computation over strings, so this suite touches no files
 * and no network -- the manifests below are literals. What is checked is the
 * contract a package author depends on: which versions a constraint admits,
 * what a shorthand spec expands to, and that a malformed manifest is rejected
 * with a reason rather than silently half-parsed.
 */

#include "oak_test_support.h"

#include "oak_pkg_manifest.h"
#include "oak_pkg_semver.h"

#include <string.h>

OAK_TEST_SUITE(package);

static oak_semver_t ver(const char* s)
{
  oak_semver_t v;
  memset(&v, 0, sizeof v);
  oak_semver_parse(s, &v);
  return v;
}

/* Non-zero when constraint `c` admits version `v`. */
static int admits(const char* c, const char* v)
{
  oak_semver_req_t req;
  oak_semver_t version;
  if (oak_semver_req_parse(c, &req) != 0)
    return -1;
  if (oak_semver_parse(v, &version) != 0)
    return -1;
  return oak_semver_req_match(&req, &version);
}


UTEST_F(package, a_version_parses_into_its_components)
{
  oak_semver_t v;
  ASSERT_EQ(0, oak_semver_parse("1.2.3", &v));
  EXPECT_EQ(1u, v.major);
  EXPECT_EQ(2u, v.minor);
  EXPECT_EQ(3u, v.patch);
  EXPECT_STREQ("", v.pre);

  ASSERT_EQ(0, oak_semver_parse("0.4.0-rc.1", &v));
  EXPECT_EQ(0u, v.major);
  EXPECT_STREQ("rc.1", v.pre);

  /* Build metadata parses and then stops existing: it is not part of a
   * version's identity, so it must not survive into a lockfile. */
  ASSERT_EQ(0, oak_semver_parse("1.2.3+build.7", &v));
  EXPECT_STREQ("", v.pre);
}

/* A version that is not exactly three components is rejected rather than
 * guessed at -- inferring the missing part is how a lock ends up pinning
 * something nobody wrote. */
UTEST_F(package, a_malformed_version_is_rejected)
{
  oak_semver_t v;
  EXPECT_EQ(-1, oak_semver_parse("1.2", &v));
  EXPECT_EQ(-1, oak_semver_parse("1.2.3.4", &v));
  EXPECT_EQ(-1, oak_semver_parse("v1.2.3", &v));
  EXPECT_EQ(-1, oak_semver_parse("1.2.x", &v));
  EXPECT_EQ(-1, oak_semver_parse("01.2.3", &v));
  EXPECT_EQ(-1, oak_semver_parse("1.2.3-", &v));
  EXPECT_EQ(-1, oak_semver_parse("", &v));
}

UTEST_F(package, versions_order_by_component_then_prerelease)
{
  const oak_semver_t a = ver("1.2.3");
  const oak_semver_t b = ver("1.10.0");
  const oak_semver_t rc = ver("1.10.0-rc.1");

  EXPECT_LT(oak_semver_cmp(&a, &b), 0);
  EXPECT_GT(oak_semver_cmp(&b, &a), 0);
  EXPECT_EQ(0, oak_semver_cmp(&a, &a));
  /* A prerelease sorts below the release it leads to. */
  EXPECT_LT(oak_semver_cmp(&rc, &b), 0);
}

UTEST_F(package, a_version_round_trips_through_its_text)
{
  char buf[64];
  const oak_semver_t v = ver("1.2.3");
  ASSERT_GT(oak_semver_format(&v, buf, sizeof buf), 0);
  EXPECT_STREQ("1.2.3", buf);

  const oak_semver_t p = ver("0.4.0-rc.1");
  ASSERT_GT(oak_semver_format(&p, buf, sizeof buf), 0);
  EXPECT_STREQ("0.4.0-rc.1", buf);

  /* Too small a buffer fails rather than emitting a truncated version, which
   * would compare as a different one. */
  EXPECT_EQ(-1, oak_semver_format(&v, buf, 3u));
}

UTEST_F(package, a_caret_constraint_admits_compatible_versions)
{
  EXPECT_EQ(1, admits("^1.2.3", "1.2.3"));
  EXPECT_EQ(1, admits("^1.2.3", "1.9.0"));
  EXPECT_EQ(0, admits("^1.2.3", "1.2.2"));
  EXPECT_EQ(0, admits("^1.2.3", "2.0.0"));
}

/* The pre-1.0 corner: below 1.0.0 the minor is what breaks, so a caret has to
 * treat it as the major or it stops meaning "compatible". */
UTEST_F(package, a_caret_below_one_pins_the_minor)
{
  EXPECT_EQ(1, admits("^0.4.1", "0.4.1"));
  EXPECT_EQ(1, admits("^0.4.1", "0.4.9"));
  EXPECT_EQ(0, admits("^0.4.1", "0.5.0"));
  EXPECT_EQ(0, admits("^0.4.1", "0.4.0"));
}

UTEST_F(package, exact_atleast_and_any_constraints)
{
  EXPECT_EQ(1, admits("1.2.3", "1.2.3"));
  EXPECT_EQ(0, admits("1.2.3", "1.2.4"));

  EXPECT_EQ(1, admits(">=1.2.3", "1.2.3"));
  EXPECT_EQ(1, admits(">=1.2.3", "9.0.0"));
  EXPECT_EQ(0, admits(">=1.2.3", "1.2.2"));

  EXPECT_EQ(1, admits("*", "0.0.1"));
  EXPECT_EQ(1, admits("*", "9.9.9"));
}

/* A range never quietly pulls in an untagged release candidate: a prerelease
 * only satisfies a constraint written against that same release. */
UTEST_F(package, a_range_does_not_admit_an_unrelated_prerelease)
{
  EXPECT_EQ(0, admits("^1.2.0", "1.3.0-rc.1"));
  EXPECT_EQ(0, admits(">=1.2.0", "2.0.0-rc.1"));
  EXPECT_EQ(1, admits(">=1.2.0-rc.1", "1.2.0-rc.1"));
}

UTEST_F(package, a_malformed_constraint_is_rejected)
{
  oak_semver_req_t r;
  EXPECT_EQ(-1, oak_semver_req_parse("~1.2.3", &r));
  EXPECT_EQ(-1, oak_semver_req_parse(">1.2.3", &r));
  EXPECT_EQ(-1, oak_semver_req_parse("^1.2", &r));
  EXPECT_EQ(-1, oak_semver_req_parse("", &r));
  EXPECT_EQ(-1, oak_semver_req_parse("*extra", &r));
}


/*
 * Dependency specs.
 */

UTEST_F(package, the_github_shorthand_expands_to_a_git_url_and_tag)
{
  oak_pkg_source_t s;
  oak_semver_req_t req;
  char err[OAK_PKG_ERROR_MAX] = { 0 };

  ASSERT_EQ(0, oak_pkg_source_parse_spec(&s, &req, OAK_A,
                                         "github:acme/oak-json@1.2.0", err,
                                         sizeof err));
  EXPECT_EQ(OAK_PKG_SOURCE_GIT, s.kind);
  EXPECT_STREQ("https://github.com/acme/oak-json.git", s.location);
  /* Tags conventionally carry the 'v' the version does not. */
  EXPECT_STREQ("v1.2.0", s.tag);
  /* The pinned tag still asks for a compatible range, so a transitive
   * dependent can raise it without anyone editing this manifest. */
  EXPECT_EQ(OAK_SEMVER_CARET, req.op);
  EXPECT_EQ(1u, req.version.major);
  EXPECT_EQ(2u, req.version.minor);

  oak_pkg_source_free(OAK_A, &s);
}

UTEST_F(package, an_https_spec_is_a_repository_or_an_archive_by_shape)
{
  oak_pkg_source_t s;
  oak_semver_req_t req;
  char err[OAK_PKG_ERROR_MAX] = { 0 };

  ASSERT_EQ(0, oak_pkg_source_parse_spec(&s, &req, OAK_A,
                                         "https://example.org/oak-bits.git",
                                         err, sizeof err));
  EXPECT_EQ(OAK_PKG_SOURCE_GIT, s.kind);
  oak_pkg_source_free(OAK_A, &s);

  ASSERT_EQ(0, oak_pkg_source_parse_spec(
                   &s, &req, OAK_A, "https://example.org/oak-bits-0.9.1.tar.gz",
                   err, sizeof err));
  EXPECT_EQ(OAK_PKG_SOURCE_URL, s.kind);
  /* Release archives almost always wrap their contents in one directory. */
  EXPECT_EQ(1, s.strip);
  oak_pkg_source_free(OAK_A, &s);
}

/* Plain http is refused outright rather than fetched and hashed: the hash
 * would only prove the bytes matched what an attacker also chose. */
UTEST_F(package, an_http_spec_is_refused)
{
  oak_pkg_source_t s;
  oak_semver_req_t req;
  char err[OAK_PKG_ERROR_MAX] = { 0 };

  EXPECT_EQ(-1, oak_pkg_source_parse_spec(&s, &req, OAK_A,
                                          "http://example.org/pkg.tar.gz", err,
                                          sizeof err));
  EXPECT_TRUE(strstr(err, "https") != OAK_NULL);
  oak_pkg_source_free(OAK_A, &s);
}

UTEST_F(package, a_local_path_spec_is_never_fetched)
{
  oak_pkg_source_t s;
  oak_semver_req_t req;
  char err[OAK_PKG_ERROR_MAX] = { 0 };

  ASSERT_EQ(0, oak_pkg_source_parse_spec(&s, &req, OAK_A, "../mylib", err,
                                         sizeof err));
  EXPECT_EQ(OAK_PKG_SOURCE_PATH, s.kind);
  EXPECT_STREQ("../mylib", s.location);
  oak_pkg_source_free(OAK_A, &s);
}

UTEST_F(package, an_unrecognised_spec_says_what_was_expected)
{
  oak_pkg_source_t s;
  oak_semver_req_t req;
  char err[OAK_PKG_ERROR_MAX] = { 0 };

  EXPECT_EQ(-1,
            oak_pkg_source_parse_spec(&s, &req, OAK_A, "acme/json", err,
                                      sizeof err));
  EXPECT_TRUE(err[0] != '\0');
  EXPECT_TRUE(strstr(err, "github:") != OAK_NULL);
  oak_pkg_source_free(OAK_A, &s);
}


/*
 * Manifests.
 */

/* Parse `json`, expecting success, and hand the manifest to the caller. */
#define PARSE_OK(m, json)                                                      \
  char err[OAK_PKG_ERROR_MAX] = { 0 };                                         \
  ASSERT_EQ_MSG(0,                                                             \
                oak_pkg_manifest_parse(&(m), OAK_A, (json), strlen(json), err, \
                                       sizeof err),                            \
                err)

UTEST_F(package, a_minimal_manifest_fills_in_its_defaults)
{
  oak_pkg_manifest_t m;
  PARSE_OK(m, "{\"name\":\"acme/json\",\"version\":\"1.2.0\"}");

  EXPECT_STREQ("acme/json", m.name);
  EXPECT_EQ(1u, m.version.major);
  EXPECT_EQ(2u, m.version.minor);
  /* The import namespace defaults to the name without its owner. */
  EXPECT_STREQ("json", m.module);
  /* Sources sit next to the manifest unless told otherwise. */
  EXPECT_STREQ(".", m.src);
  EXPECT_EQ(OAK_SEMVER_ANY, m.oak_req.op);
  EXPECT_EQ(0, m.has_native);
  EXPECT_EQ((usize)0, oak_size(m.deps));

  oak_pkg_manifest_free(&m);
}

UTEST_F(package, a_manifest_reads_every_dependency_form)
{
  oak_pkg_manifest_t m;
  PARSE_OK(m,
           "{\"name\":\"acme/app\",\"version\":\"0.1.0\",\"src\":\"src\","
           "\"oak\":\">=1.0.0\",\"deps\":{"
           "\"json\":\"github:acme/oak-json@1.2.0\","
           "\"bits\":{\"url\":\"https://x.org/b.tar.gz\",\"sha256\":"
           "\"9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\","
           "\"strip\":0},"
           "\"gitdep\":{\"git\":\"https://x.org/g.git\",\"tag\":\"v2.0.0\"},"
           "\"local\":{\"path\":\"../mylib\"}}}");

  EXPECT_STREQ("src", m.src);
  EXPECT_EQ(OAK_SEMVER_ATLEAST, m.oak_req.op);
  ASSERT_EQ((usize)4, oak_size(m.deps));

  const oak_pkg_dep_t* json = oak_pkg_manifest_dep(&m, "json");
  ASSERT_TRUE(json != OAK_NULL);
  EXPECT_EQ(OAK_PKG_SOURCE_GIT, json->source.kind);
  EXPECT_STREQ("v1.2.0", json->source.tag);

  const oak_pkg_dep_t* bits = oak_pkg_manifest_dep(&m, "bits");
  ASSERT_TRUE(bits != OAK_NULL);
  EXPECT_EQ(OAK_PKG_SOURCE_URL, bits->source.kind);
  EXPECT_EQ(0, bits->source.strip);
  EXPECT_TRUE(bits->source.sha256 != OAK_NULL);

  const oak_pkg_dep_t* g = oak_pkg_manifest_dep(&m, "gitdep");
  ASSERT_TRUE(g != OAK_NULL);
  EXPECT_STREQ("v2.0.0", g->source.tag);

  const oak_pkg_dep_t* local = oak_pkg_manifest_dep(&m, "local");
  ASSERT_TRUE(local != OAK_NULL);
  EXPECT_EQ(OAK_PKG_SOURCE_PATH, local->source.kind);

  EXPECT_TRUE(oak_pkg_manifest_dep(&m, "absent") == OAK_NULL);

  oak_pkg_manifest_free(&m);
}

UTEST_F(package, a_native_section_records_the_library_and_abi)
{
  oak_pkg_manifest_t m;
  PARSE_OK(m,
           "{\"name\":\"acme/zlib\",\"version\":\"1.0.0\","
           "\"native\":{\"abi\":1,\"lib\":\"zlib\"}}");

  EXPECT_EQ(1, m.has_native);
  EXPECT_EQ(1, m.native.abi);
  EXPECT_STREQ("zlib", m.native.lib);
  /* The per-platform directory has a default so most manifests omit it. */
  EXPECT_STREQ("native", m.native.dir);

  oak_pkg_manifest_free(&m);
}

/* Each of these is a mistake an author can plausibly make, and each has to be
 * named rather than absorbed into a half-built manifest. */
UTEST_F(package, a_malformed_manifest_is_rejected_with_a_reason)
{
  static const char* const bad[] = {
    /* not an object */
    "[]",
    /* missing name / version */
    "{\"version\":\"1.0.0\"}",
    "{\"name\":\"acme/json\"}",
    /* version is not a version */
    "{\"name\":\"acme/json\",\"version\":\"1.2\"}",
    /* wrong type for a known field */
    "{\"name\":\"acme/json\",\"version\":\"1.0.0\",\"src\":7}",
    /* a namespace has to be one import segment */
    "{\"name\":\"acme/json\",\"version\":\"1.0.0\",\"module\":\"a.b\"}",
    /* a dependency with no location */
    "{\"name\":\"a/b\",\"version\":\"1.0.0\",\"deps\":{\"x\":{}}}",
    /* two locations at once */
    "{\"name\":\"a/b\",\"version\":\"1.0.0\",\"deps\":{\"x\":"
    "{\"git\":\"https://x/g.git\",\"path\":\"../y\"}}}",
    /* an unpinned git branch is not reproducible */
    "{\"name\":\"a/b\",\"version\":\"1.0.0\",\"deps\":{\"x\":"
    "{\"git\":\"https://x/g.git\"}}}",
    /* tag and rev both */
    "{\"name\":\"a/b\",\"version\":\"1.0.0\",\"deps\":{\"x\":"
    "{\"git\":\"https://x/g.git\",\"tag\":\"v1\",\"rev\":\"abc\"}}}",
    /* a sha256 that is not a digest */
    "{\"name\":\"a/b\",\"version\":\"1.0.0\",\"deps\":{\"x\":"
    "{\"url\":\"https://x/a.tar.gz\",\"sha256\":\"nope\"}}}",
    /* a dotted alias would claim a namespace it does not own */
    "{\"name\":\"a/b\",\"version\":\"1.0.0\",\"deps\":{\"a.b\":"
    "{\"path\":\"../y\"}}}",
    /* native without an abi */
    "{\"name\":\"a/b\",\"version\":\"1.0.0\",\"native\":{\"lib\":\"z\"}}",
    /* malformed JSON */
    "{\"name\":",
  };

  for (usize i = 0; i < OAK_COUNT_OF(bad); ++i)
  {
    oak_pkg_manifest_t m;
    char err[OAK_PKG_ERROR_MAX] = { 0 };
    EXPECT_EQ_MSG(-1,
                  oak_pkg_manifest_parse(&m, OAK_A, bad[i], strlen(bad[i]), err,
                                         sizeof err),
                  bad[i]);
    EXPECT_TRUE(err[0] != '\0');
    /* A rejected manifest owns nothing, so freeing it again is a no-op and
     * the fixture's allocator sees no leak either way. */
    oak_pkg_manifest_free(&m);
  }
}

/* An unknown key is left alone: a package written for a newer oak-pkg must
 * stay readable rather than failing on a field this build has never heard of. */
UTEST_F(package, an_unknown_field_is_ignored)
{
  oak_pkg_manifest_t m;
  PARSE_OK(m,
           "{\"name\":\"acme/json\",\"version\":\"1.0.0\","
           "\"keywords\":[\"json\"],\"future\":{\"x\":1}}");
  EXPECT_STREQ("acme/json", m.name);
  oak_pkg_manifest_free(&m);
}
