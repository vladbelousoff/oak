#pragma once

/*
 * Where fetched packages live.
 *
 * The cache is content-addressed -- a git checkout by its commit, an archive by
 * its sha256 -- which is what lets it be shared across every project on the
 * machine and never invalidated.  A directory that exists is, by construction,
 * the right bytes, so `oak` can use it without consulting the network and
 * `oak-pkg` can skip a fetch by testing for it.
 *
 * Names keep a human-readable prefix in front of the digest, because a cache
 * anyone might have to look inside should not be a wall of hex.
 */

#include "oak_allocator.h"
#include "oak_pkg_manifest.h"
#include "oak_types.h"

/* Root of the package cache: $OAK_PACKAGE_CACHE if set, else <home>/.oak/
 * packages.  Caller owns the result; null when no home directory can be
 * determined and the variable is unset. */
char* oak_pkg_cache_root(oak_allocator_t* a, oak_source_loc_t loc);

/* Directory a fetched source unpacks into, under `root`.
 *
 * For git this needs the resolved commit rather than the tag: a tag can be
 * moved, and a cache entry that could change meaning is not a cache entry.
 * `rev` is therefore required for OAK_PKG_SOURCE_GIT, and `sha256` for
 * OAK_PKG_SOURCE_URL.  Returns null for a path source, which is never cached,
 * or when the required digest is missing. */
char* oak_pkg_cache_dir(oak_allocator_t* a,
                        const char* root,
                        const oak_pkg_source_t* source,
                        const char* rev,
                        const char* sha256,
                        oak_source_loc_t loc);

/* Path to a native package's shared library for the running platform, e.g.
 * "<pkg>/native/linux-x86_64/libzlib.so".  Applies the platform's own naming --
 * `lib` prefix and `.so`/`.dylib`/`.dll` suffix -- so a manifest names the
 * library once and every platform's artifact is derived rather than listed.
 * Caller owns the result. */
char* oak_pkg_native_lib_path(oak_allocator_t* a,
                              const char* package_dir,
                              const oak_pkg_native_t* native,
                              oak_source_loc_t loc);
