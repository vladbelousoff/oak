#pragma once

/*
 * oak.lock -- the resolved graph, written by oak-pkg and read by everything.
 *
 * The lock is what makes running a program deterministic and offline: it names
 * one exact tree per package, by commit or by digest, and `oak` consults
 * nothing else.  Resolution, version constraints and the network all belong to
 * `oak-pkg install`; by the time a program runs, every question has already
 * been answered and written down.
 *
 * It is generated, not hand-written, so parsing is looser about presentation
 * than the manifest is -- but never about identity: an entry with no commit or
 * no digest is rejected rather than fetched on trust.
 */

#include "oak_allocator.h"
#include "oak_container.h"
#include "oak_pkg_manifest.h"
#include "oak_types.h"
#include "oak_vector.h"

/* Lock format this build writes and understands. */
#define OAK_PKG_LOCK_VERSION 1

typedef struct oak_pkg_lock_entry oak_pkg_lock_entry_t;
struct oak_pkg_lock_entry
{
  /* All owned. */
  char* name;
  char* module;
  char* src;
  oak_semver_t version;
  oak_pkg_source_kind_t kind;
  /* Git URL or archive URL; the key callers look an entry up by. */
  char* location;
  /* Resolved commit, for a git entry. */
  char* rev;
  /* Archive digest, for a URL entry. */
  char* sha256;
  int strip;
};

typedef struct oak_pkg_lock oak_pkg_lock_t;
struct oak_pkg_lock
{
  oak_allocator_t* allocator;
  /* Vector of oak_pkg_lock_entry_t. */
  oak_container_t* entries;
};

/* Read the lock at `path`.  A missing file is not an error: `out` comes back
 * empty, because a project with only path dependencies never needs one. */
int oak_pkg_lock_read(oak_pkg_lock_t* out,
                      oak_allocator_t* a,
                      const char* path,
                      char* err,
                      usize err_cap);

/* Write `lock` to `path`, replacing whatever is there.  Entries are written in
 * the order they appear, which the resolver has already sorted by name so that
 * a regenerated lock diffs against the previous one meaningfully. */
int oak_pkg_lock_write(const oak_pkg_lock_t* lock,
                       const char* path,
                       char* err,
                       usize err_cap);

void oak_pkg_lock_free(oak_pkg_lock_t* lock);

/* The entry for a git or archive URL, or null. */
const oak_pkg_lock_entry_t* oak_pkg_lock_find(const oak_pkg_lock_t* lock,
                                              const char* location);
