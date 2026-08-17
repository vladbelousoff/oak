#pragma once

/*
 * Turning a manifest's dependencies into a lockfile.
 *
 * Resolution is minimal version selection: every dependent states a minimum,
 * and the version chosen is the highest minimum anyone asked for -- never the
 * highest that exists.  There is no search and no backtracking, so the answer
 * is a function of the manifests alone.  The practical consequence is the one
 * worth having: publishing a new release cannot change what an existing project
 * builds, because nobody's manifest asked for it.
 *
 * Fetching is a callback rather than a call.  The resolver needs a dependency's
 * manifest to learn its version and its own dependencies, which means it has to
 * materialize the tree -- but keeping the transport out of here is what lets
 * the algorithm be tested against a stub instead of the network, and keeps
 * libcurl out of everything except the one tool that downloads.
 */

#include "oak_allocator.h"
#include "oak_pkg_lock.h"
#include "oak_pkg_manifest.h"
#include "oak_types.h"

typedef struct oak_pkg_fetched oak_pkg_fetched_t;
struct oak_pkg_fetched
{
  /* Directory the materialized package sits in.  Owned by the callback's
   * allocator; the resolver frees it. */
  char* dir;
  /* The commit that was checked out, for a git source. */
  char* rev;
  /* The digest of the archive that was unpacked, for a URL source. */
  char* sha256;
};

/* Materialize `source` and describe where it landed.  Returns 0, or -1 with a
 * reason in `err`. */
typedef int (*oak_pkg_fetch_fn)(void* ctx,
                                const oak_pkg_source_t* source,
                                oak_pkg_fetched_t* out,
                                char* err,
                                usize err_cap);

/* Resolve `root`'s graph into `out`, which the caller then writes to oak.lock.
 *
 * `root_dir` is where `root`'s manifest lives, which is what relative path
 * dependencies resolve against.  Path dependencies are walked -- their own
 * dependencies are part of the graph -- but never locked: they are whatever is
 * on disk, and recording a digest for something a developer edits in place
 * would be a lie the next build has to discover.
 *
 * Returns 0, or -1 with a reason in `err`.  `out` is left released on failure. */
int oak_pkg_resolve(oak_allocator_t* a,
                    const oak_pkg_manifest_t* root,
                    const char* root_dir,
                    oak_pkg_fetch_fn fetch,
                    void* ctx,
                    oak_pkg_lock_t* out,
                    char* err,
                    usize err_cap);
