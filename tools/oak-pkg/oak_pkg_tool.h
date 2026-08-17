#pragma once

/*
 * Shared state for the oak-pkg commands.
 *
 * The tool's whole job is to answer questions the runtime refuses to ask: what
 * version, from where, and are those the right bytes.  `oak` reads oak.lock and
 * nothing else, so everything here happens once, on purpose, when a person runs
 * a command -- never as a side effect of running a program.
 */

#include "oak_allocator.h"
#include "oak_pkg_lock.h"
#include "oak_pkg_manifest.h"
#include "oak_pkg_resolve.h"
#include "oak_types.h"

typedef struct oak_pkg_tool oak_pkg_tool_t;
struct oak_pkg_tool
{
  oak_allocator_t* a;
  /* Directory holding oak.json, and the two files in it.  All owned. */
  char* project_dir;
  char* manifest_path;
  char* lock_path;
  /* Root of the shared package cache.  Owned. */
  char* cache_root;
  /* The lock as it was on entry, consulted so an install reproduces what is
   * already pinned instead of re-resolving tags. */
  oak_pkg_lock_t lock;
  int have_lock;
  /* Set by `update`: ignore the existing lock and take each tag freshly. */
  int refresh;
  /* $OAK_OFFLINE: fail rather than reach the network. */
  int offline;
  int quiet;
  /* Trees already materialized by this process, so `add` -- which fetches once
   * to read the package's name and again while resolving -- clones once.
   * Vector of oak_pkg_memo_t, owned by oak_pkg_fetch.c. */
  oak_container_t* fetched;
};

/* Release the fetch memo.  Called by oak_pkg_tool_close. */
void oak_pkg_tool_forget(oak_pkg_tool_t* t);

/* Locate the project containing the current directory and fill `out`.  Returns
 * 0, or -1 after printing why. */
int oak_pkg_tool_open(oak_pkg_tool_t* out, oak_allocator_t* a);

void oak_pkg_tool_close(oak_pkg_tool_t* t);

/* Progress, suppressed by --quiet.  Goes to stderr so that the output of
 * `cache path` stays pipeable. */
void oak_pkg_say(const oak_pkg_tool_t* t, const char* fmt, ...);

/* Materialize `source` into the cache, or reuse what is there.  This is the
 * oak_pkg_fetch_fn the resolver drives; `ctx` is an oak_pkg_tool_t. */
int oak_pkg_tool_fetch(void* ctx,
                       const oak_pkg_source_t* source,
                       oak_pkg_fetched_t* out,
                       char* err,
                       usize err_cap);

/* Download `url`, check it against `expect_sha256` when that is non-null, and
 * unpack it into `dest`, dropping `strip` leading path components.  Writes the
 * digest of what actually arrived into `out_sha256`.
 *
 * Defined in oak_pkg_http.c, which is the only file in the project that talks
 * to the network. */
int oak_pkg_http_fetch(oak_allocator_t* a,
                       const char* url,
                       const char* expect_sha256,
                       int strip,
                       const char* dest,
                       char out_sha256[65],
                       char* err,
                       usize err_cap);

/* Whether this build can fetch archives at all.  HTTPS support needs libcurl
 * and libarchive; a build without them still handles git and path dependencies
 * completely, and says so rather than failing obscurely. */
int oak_pkg_http_available(void);

int oak_pkg_cmd_init(oak_pkg_tool_t* t, int argc, const char* const* argv);
int oak_pkg_cmd_add(oak_pkg_tool_t* t, int argc, const char* const* argv);
int oak_pkg_cmd_remove(oak_pkg_tool_t* t, int argc, const char* const* argv);
int oak_pkg_cmd_install(oak_pkg_tool_t* t, int argc, const char* const* argv);
int oak_pkg_cmd_update(oak_pkg_tool_t* t, int argc, const char* const* argv);
int oak_pkg_cmd_tree(oak_pkg_tool_t* t, int argc, const char* const* argv);
int oak_pkg_cmd_check(oak_pkg_tool_t* t, int argc, const char* const* argv);
int oak_pkg_cmd_cache(oak_pkg_tool_t* t, int argc, const char* const* argv);
