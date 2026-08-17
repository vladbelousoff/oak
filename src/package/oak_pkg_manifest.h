#pragma once

/*
 * oak.json -- what a package declares about itself.
 *
 * The manifest is the only file an author writes by hand, so parsing is strict
 * and every rejection says what was wrong and where.  A field the parser does
 * not know is left alone rather than rejected: a newer oak-pkg adding a key
 * must not make the package unreadable to an older one.
 */

#include "oak_allocator.h"
#include "oak_container.h"
#include "oak_pkg_semver.h"
#include "oak_vector.h"
#include "oak_types.h"

/* Longest error this layer reports.  Matches oak_diagnostic_t::message so a
 * package error can be copied into a diagnostic without a second buffer. */
#define OAK_PKG_ERROR_MAX 512

typedef enum oak_pkg_source_kind oak_pkg_source_kind_t;
enum oak_pkg_source_kind
{
  OAK_PKG_SOURCE_PATH, /* { "path": "../mylib" }        -- local, never fetched */
  OAK_PKG_SOURCE_GIT,  /* { "git": "...", "tag"|"rev" } */
  OAK_PKG_SOURCE_URL,  /* { "url": "https://...", "sha256": "..." } */
};

typedef struct oak_pkg_source oak_pkg_source_t;
struct oak_pkg_source
{
  oak_pkg_source_kind_t kind;
  /* Path, git URL, or archive URL, by kind.  Owned. */
  char* location;
  /* Git only, exactly one of which is set after parsing.  Owned. */
  char* tag;
  char* rev;
  /* URL only.  Lowercase hex, 64 chars, or null when the author has not
   * recorded one yet and `oak-pkg add` is expected to fill it in.  Owned. */
  char* sha256;
  /* URL only: leading path components to drop when unpacking.  Defaults to 1,
   * because virtually every release archive wraps its contents in a single
   * versioned directory. */
  int strip;
};

typedef struct oak_pkg_dep oak_pkg_dep_t;
struct oak_pkg_dep
{
  /* The import namespace this dependency answers to.  Owned. */
  char* alias;
  oak_pkg_source_t source;
  /* Version the dependent asks for.  Shorthand carries it (`@1.2.0` becomes
   * ^1.2.0); the long forms default to "*" because the tag or hash already
   * pins exactly one tree. */
  oak_semver_req_t req;
};

typedef struct oak_pkg_native oak_pkg_native_t;
struct oak_pkg_native
{
  /* Plugin ABI the shared library was built against. */
  int abi;
  /* Library base name, without the platform's lib prefix or extension.  Owned. */
  char* lib;
  /* Directory holding the per-platform subdirectories.  Owned. */
  char* dir;
};

typedef struct oak_pkg_manifest oak_pkg_manifest_t;
struct oak_pkg_manifest
{
  oak_allocator_t* allocator;
  /* "acme/json".  Owned; null only for an unnamed root project. */
  char* name;
  oak_semver_t version;
  /* Import namespace the package claims, defaulting to the last segment of
   * `name`.  Owned. */
  char* module;
  /* Directory dotted names resolve against, relative to the manifest.  Owned;
   * "." when unset. */
  char* src;
  /* Owned; null when unset. */
  char* license;
  /* Runtime the package needs; "*" when unset. */
  oak_semver_req_t oak_req;
  /* Zero when the package ships no shared library. */
  int has_native;
  oak_pkg_native_t native;
  /* Vector of oak_pkg_dep_t. */
  oak_container_t* deps;
};

/* Expand a dependency spec into a source and a version request.
 *
 * Accepts the shorthand `github:owner/repo@1.2.0` (and `gitlab:`), a bare
 * https git URL, and a local `./path`.  `spec` is not modified.  Returns 0, or
 * -1 with a reason in `err`. */
int oak_pkg_source_parse_spec(oak_pkg_source_t* out,
                              oak_semver_req_t* out_req,
                              oak_allocator_t* a,
                              const char* spec,
                              char* err,
                              usize err_cap);

void oak_pkg_source_free(oak_allocator_t* a, oak_pkg_source_t* s);

/* Parse manifest text.  `json` need not be NUL-terminated; `len` bounds it.
 * On success `out` owns everything and must be released with
 * oak_pkg_manifest_free.  On failure `out` is left zeroed and `err` says why. */
int oak_pkg_manifest_parse(oak_pkg_manifest_t* out,
                           oak_allocator_t* a,
                           const char* json,
                           usize len,
                           char* err,
                           usize err_cap);

/* Read and parse the manifest at `path`. */
int oak_pkg_manifest_read(oak_pkg_manifest_t* out,
                          oak_allocator_t* a,
                          const char* path,
                          char* err,
                          usize err_cap);

/* Read the manifest of the package in `dir`.
 *
 * Separate from the above for the sake of one message: a directory with no
 * oak.json is not a package, and saying that is far more use than reporting
 * that some path could not be opened.  `what` names the thing in the reader's
 * terms -- a URL, a dependency alias -- and may be null for `dir` itself. */
int oak_pkg_manifest_read_dir(oak_pkg_manifest_t* out,
                              oak_allocator_t* a,
                              const char* dir,
                              const char* what,
                              char* err,
                              usize err_cap);

void oak_pkg_manifest_free(oak_pkg_manifest_t* m);

/* Find a dependency by its import namespace, or null. */
const oak_pkg_dep_t* oak_pkg_manifest_dep(const oak_pkg_manifest_t* m,
                                          const char* alias);

/* Write a new manifest at `path`.  `module` may be null to let it default. */
int oak_pkg_manifest_init_file(oak_allocator_t* a,
                               const char* path,
                               const char* name,
                               const char* module,
                               const char* src,
                               char* err,
                               usize err_cap);

/* Add `alias` to the manifest at `path`, replacing any dependency already
 * using that name.  The rest of the file -- including fields this build does
 * not recognise -- is preserved, because a manifest is hand-written and
 * regenerating it would quietly discard whatever a newer oak-pkg put there. */
int oak_pkg_manifest_add_dep(oak_allocator_t* a,
                             const char* path,
                             const char* alias,
                             const oak_pkg_source_t* source,
                             const oak_semver_req_t* req,
                             char* err,
                             usize err_cap);

/* Remove `alias`.  Fails when there is no such dependency, rather than
 * reporting success for something that did not happen. */
int oak_pkg_manifest_remove_dep(oak_allocator_t* a,
                                const char* path,
                                const char* alias,
                                char* err,
                                usize err_cap);
