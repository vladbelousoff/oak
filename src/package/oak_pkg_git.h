#pragma once

/*
 * Fetching a git dependency by running git.
 *
 * Deliberately the git binary rather than a library.  A developer's git already
 * knows their SSH keys, credential helper, corporate CA bundle, proxy, and any
 * `url.<base>.insteadOf` rewrites; a linked library knows none of that and
 * would have to re-implement all of it badly.  Cargo shells out for the same
 * reason.  Set OAK_GIT to point at a different binary.
 */

#include "oak_allocator.h"
#include "oak_pkg_manifest.h"
#include "oak_types.h"

/* A commit is 40 hex characters, plus the terminator. */
#define OAK_PKG_REV_SIZE 41u

/* The git binary to run: $OAK_GIT when set, else "git". */
const char* oak_pkg_git_program(void);

/* Check out `source` into `dest`, which must not already exist.
 *
 * `source` must name a tag or a rev; a branch is not accepted anywhere in this
 * system, because what it points at can change and a cache entry that can
 * change meaning is not a cache entry.  On success `out_rev` holds the commit
 * that was actually checked out, which is what gets locked -- a tag is resolved
 * here exactly once and never trusted again. */
int oak_pkg_git_fetch(oak_allocator_t* a,
                      const oak_pkg_source_t* source,
                      const char* dest,
                      char out_rev[OAK_PKG_REV_SIZE],
                      char* err,
                      usize err_cap);

/* Non-zero when `s` looks like a full 40-character commit hash. */
int oak_pkg_git_is_rev(const char* s);
