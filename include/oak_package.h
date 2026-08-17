#pragma once

/*
 * Packages: making a program's dependencies importable.
 *
 * A project is a directory with an `oak.json` beside (or above) the program
 * being run.  Opening a package set reads that manifest, its `oak.lock`, and
 * every manifest they lead to; applying it turns each declared dependency into
 * a module mount, so `import json.lexer` resolves into the dependency's own
 * source directory instead of a path relative to the importing file.
 *
 * A program with no manifest anywhere above it is not an error -- the set comes
 * back empty and every import resolves exactly as it did before packages
 * existed.  That is the case a single-file script is in, and it stays free.
 *
 * Usage, between registering native bindings and loading the program:
 *
 *     oak_package_set_t* pkgs = oak_package_set_open(script, &alloc, &err);
 *     if (!pkgs) { ...report err... }
 *     if (oak_package_set_apply(pkgs, &opts, &err) != 0) { ...report err... }
 *     oak_module_loader_load_program(script, &opts, &registry, &result);
 *
 * Lifetime: the set must outlive the compile options it was applied to, and is
 * closed last of all -- after oak_compile_options_free -- because a native
 * package's bindings live in a shared library the set is holding open.
 *
 *     oak_vm_free(&vm);
 *     oak_module_registry_free(&registry);
 *     oak_compile_options_free(&opts);
 *     oak_package_set_close(pkgs);
 */

#include "oak_allocator.h"
#include "oak_diagnostic.h"
#include "oak_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oak_compile_options oak_compile_options_t;

typedef struct oak_package_set oak_package_set_t;

/* Discover the project containing `entry_path` and read its dependency graph.
 *
 * Searches `entry_path`'s directory and each parent for an `oak.json`, so a
 * program in a subdirectory still finds the project it belongs to.  Returns a
 * set -- empty when there is no manifest -- or null on a real failure, with
 * `err` filled in when non-null. */
OAK_API oak_package_set_t* oak_package_set_open(const char* entry_path,
                                                oak_allocator_t* a,
                                                oak_diagnostic_t* err);

/* Mount every package in the set onto `opts`, so the module loader can resolve
 * imports through it, and load the shared library of any package that ships
 * one.  Returns 0, or -1 with `err` filled in. */
OAK_API int oak_package_set_apply(oak_package_set_t* set,
                                  oak_compile_options_t* opts,
                                  oak_diagnostic_t* err);

/* Allow or refuse loading native packages, before applying the set.  Enabled by
 * default.  Refusing makes a graph that needs one an error rather than quietly
 * running without it: a package ships a library because its `.oak` declarations
 * have no bodies, so proceeding would fail later and less clearly. */
OAK_API void oak_package_set_allow_plugins(oak_package_set_t* set, int allow);

/* Directory holding the project's `oak.json`, or null when none was found.
 * Borrowed from the set. */
OAK_API const char* oak_package_set_root(const oak_package_set_t* set);

/* Number of packages in the set, including the project itself.  Zero when no
 * manifest was found. */
OAK_API int oak_package_set_count(const oak_package_set_t* set);

OAK_API void oak_package_set_close(oak_package_set_t* set);

#ifdef __cplusplus
}
#endif
