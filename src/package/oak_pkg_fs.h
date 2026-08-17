#pragma once

/*
 * The filesystem operations fetching needs and the module loader never did.
 *
 * `oak` only ever reads paths, so oak_module_loader_path.c stops at exists/
 * canonicalize/join.  Populating a cache means creating directories, throwing
 * away a failed attempt, and publishing a finished one -- the last of which is
 * the reason this file exists at all: a fetch writes into a temporary directory
 * and is renamed into place only once it is complete and verified, so an
 * interrupted download can never be mistaken for a cached package.
 */

#include "oak_allocator.h"
#include "oak_container.h"
#include "oak_types.h"
#include "oak_vector.h"

/* Create `path` and any missing parents.  Succeeds if it already exists as a
 * directory.  Returns 0 or -1. */
int oak_pkg_mkdir_p(const char* path);

/* Delete `path` and everything under it.  A missing path is success: callers
 * use this to clean up after a failure that may not have created anything. */
int oak_pkg_rmtree(oak_allocator_t* a, const char* path);

/* Move `from` to `to`, which must not exist.  This is the publish step, so it
 * must be atomic enough that a reader never sees a partial tree; both paths
 * therefore have to be on the same filesystem, which is why fetching stages
 * inside the cache root rather than in the system temp directory. */
int oak_pkg_rename(const char* from, const char* to);

/* A directory name that does not exist yet, inside `parent`.  Uniquely named so
 * two concurrent `oak-pkg install` runs stage separately, and inside the cache
 * rather than the system temp directory so publishing it is a rename within one
 * filesystem.  Caller owns the result; the directory itself is not created. */
char* oak_pkg_stage_path(oak_allocator_t* a,
                         const char* parent,
                         oak_source_loc_t loc);

/* Non-zero when `path` is a directory. */
int oak_pkg_is_dir(const char* path);

/* Names of the entries directly inside `path`, as a vector of owned char*, or
 * null when it cannot be read.  Used to tell someone which platforms a package
 * actually ships rather than only which one they wanted. */
oak_container_t* oak_pkg_list_dir(oak_allocator_t* a, const char* path);

void oak_pkg_list_free(oak_allocator_t* a, oak_container_t* list);

/* Write `len` bytes to `path`, creating or truncating it.  Returns 0 or -1. */
int oak_pkg_write_file(const char* path, const void* data, usize len);
