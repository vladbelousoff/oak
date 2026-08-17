#pragma once

/*
 * Semantic versions and the constraints a manifest writes against them.
 *
 * Deliberately a small subset: `1.2.3`, `^1.2.3`, `>=1.2.3` and `*`.  Oak
 * resolves dependencies by minimal version selection, which asks only "is this
 * version acceptable" and "which of two is higher" -- it never searches, so the
 * expressive range grammars other ecosystems need (unions, exclusions,
 * hyphen ranges) would be machinery with nothing to drive it.
 *
 * Build metadata (`+sha.1234`) is accepted and ignored, as the spec requires.
 * Prereleases order below their release, so 1.2.0-rc.1 < 1.2.0.
 */

#include "oak_types.h"

/* Longest prerelease this keeps; anything longer is rejected at parse time
 * rather than silently truncated into a different version. */
#define OAK_SEMVER_PRE_MAX 32

typedef struct oak_semver oak_semver_t;
struct oak_semver
{
  u32 major;
  u32 minor;
  u32 patch;
  /* Prerelease without its leading '-', or "" for a release. */
  char pre[OAK_SEMVER_PRE_MAX];
};

typedef enum oak_semver_op oak_semver_op_t;
enum oak_semver_op
{
  OAK_SEMVER_ANY,     /* *        -- any version                          */
  OAK_SEMVER_EXACT,   /* 1.2.3    -- exactly this one                     */
  OAK_SEMVER_CARET,   /* ^1.2.3   -- at least this, same compatible range */
  OAK_SEMVER_ATLEAST, /* >=1.2.3  -- at least this, any later major       */
};

typedef struct oak_semver_req oak_semver_req_t;
struct oak_semver_req
{
  oak_semver_op_t op;
  oak_semver_t version; /* unused when op is OAK_SEMVER_ANY */
};

/* Parse "MAJOR.MINOR.PATCH[-PRE][+BUILD]".  Every component is required: a
 * two-part "1.2" is rejected, because guessing the third one is how a lockfile
 * ends up pinning something nobody wrote.  Returns 0, or -1 leaving `out`
 * untouched. */
int oak_semver_parse(const char* s, oak_semver_t* out);

/* Total order: negative, zero or positive as `a` sorts before, with, or after
 * `b`.  A prerelease sorts before the release it leads to. */
int oak_semver_cmp(const oak_semver_t* a, const oak_semver_t* b);

/* Write "1.2.3" or "1.2.3-rc.1" into `buf`.  Returns the length written, or -1
 * if `cap` is too small.  A version always fits in 64 bytes. */
int oak_semver_format(const oak_semver_t* v, char* buf, usize cap);

/* Parse a constraint: "*", "1.2.3", "^1.2.3" or ">=1.2.3".  Surrounding spaces
 * are allowed.  Returns 0, or -1. */
int oak_semver_req_parse(const char* s, oak_semver_req_t* out);

/* Non-zero when `v` satisfies `r`.
 *
 * Caret follows the usual "compatible with" rule, including its awkward corner:
 * below 1.0.0 the minor acts as the major, so ^0.4.1 admits 0.4.x but not
 * 0.5.0, because pre-1.0 projects break things in minor releases and pretending
 * otherwise is how a caret constraint stops meaning anything.
 *
 * A prerelease only ever satisfies a constraint pinned to that same
 * major.minor.patch, so `^1.2.0` never quietly pulls in `1.3.0-rc.1`. */
int oak_semver_req_match(const oak_semver_req_t* r, const oak_semver_t* v);
