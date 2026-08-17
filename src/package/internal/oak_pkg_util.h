#pragma once

/* Small shared helpers for the package layer. */

#include "oak_allocator.h"
#include "oak_types.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Duplicate `s` with `a`, blaming `loc` -- callers pass their own OAK_HERE so
 * the tracking allocator names the field being copied, not this helper. Null in,
 * null out. */
char* oak_pkg_strdup(oak_allocator_t* a, const char* s, oak_source_loc_t loc);

/* Duplicate the first `n` bytes of `s` and NUL-terminate. */
char* oak_pkg_strndup(oak_allocator_t* a,
                      const char* s,
                      usize n,
                      oak_source_loc_t loc);

/* Write a printf-formatted message into `err` when `err` is non-null, and
 * return -1, which is what every failing caller returns. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
int oak_pkg_fail(char* err, usize err_cap, const char* fmt, ...);

/* Read a whole file into a NUL-terminated buffer allocated with `a`. Sets
 * `*out_len` to the byte count (excluding the terminator). Returns null and
 * fills `err` on failure. */
char* oak_pkg_read_file(oak_allocator_t* a,
                        const char* path,
                        usize* out_len,
                        char* err,
                        usize err_cap,
                        oak_source_loc_t loc);

/* Join `base` and `rel` with exactly one separator.
 *
 * The module loader has path_join already, but it is internal to acorn and not
 * exported, and oak-pkg links acorn as a shared library -- so the package layer
 * carries its own rather than widening the public API for a string operation. */
char* oak_pkg_path_join(oak_allocator_t* a,
                        const char* base,
                        const char* rel,
                        oak_source_loc_t loc);

/* Absolute, symlink-resolved form of `path`, or null when it does not exist.
 * Package identity is the resolved directory, so two aliases reaching the same
 * checkout have to produce the same string here. */
char* oak_pkg_path_abs(oak_allocator_t* a,
                       const char* path,
                       oak_source_loc_t loc);

/* Directory part of `path`, without its trailing separator. */
char* oak_pkg_path_dirname(oak_allocator_t* a,
                           const char* path,
                           oak_source_loc_t loc);
