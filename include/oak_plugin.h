#pragma once

/*
 * The interface a compiled package's shared library exposes.
 *
 * A native package is an ordinary package that also ships a shared library.
 * Its `.oak` source declares the functions and types with no bodies -- exactly
 * the form the standard library's own `io.oak` uses -- and the library supplies
 * the implementations by registering them on `oak_compile_options_t`.  That is
 * why `bind` has the shape it does: it is the same code an embedder already
 * writes with `oak_bind_module`, `oak_bind_type` and `oak_bind_fns`, moved
 * into a library that ships separately from the host.  Everything downstream of it --
 * type checking, the bodyless-declaration validation, module exports -- is
 * unchanged and unaware that a plugin was involved.
 *
 * A plugin defines exactly one exported symbol:
 *
 *     static int bind(oak_compile_options_t* opts) { ...oak_bind_* calls... }
 *
 *     static const oak_plugin_t plugin = {
 *       OAK_PLUGIN_ABI, "acme/zlib", "1.2.0", OAK_VERSION_STRING, bind,
 *     };
 *
 *     OAK_PLUGIN_EXPORT const oak_plugin_t* oak_plugin_main(void)
 *     {
 *       return &plugin;
 *     }
 *
 * Lifetime: everything `bind` registers is owned by the compile options, but
 * the strings and function pointers inside live in the shared library.  The
 * library therefore has to outlive the options, which is why a package set is
 * closed after oak_compile_options_free and not before.
 */

#include "oak_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever the struct below or the expectations around `bind` change.
 * A plugin built against a different number is refused rather than called:
 * there is no version of "try it and see" that fails safely across an ABI. */
#define OAK_PLUGIN_ABI 2u

/* The symbol looked up in the shared library. */
#define OAK_PLUGIN_ENTRY "oak_plugin_main"

typedef struct oak_compile_options oak_compile_options_t;

typedef struct oak_plugin oak_plugin_t;
struct oak_plugin
{
  /* Must equal OAK_PLUGIN_ABI. */
  u32 abi;
  /* The package this library belongs to, e.g. "acme/zlib".  Used in
   * diagnostics, and checked against the package that loaded it. */
  const char* name;
  /* The package's own version, e.g. "1.2.0". */
  const char* version;
  /* OAK_VERSION_STRING the library was compiled against.  A mismatched major
   * is refused: the binding structs are part of the ABI too. */
  const char* oak_version;
  /* Register this package's native functions and types.  Returns 0, or
   * non-zero to refuse to load, in which case nothing should have been
   * registered. */
  int (*bind)(oak_compile_options_t* opts);
};

/* Marks the entry point as visible outside the shared library.  Keyed only on
 * the compiler and platform, never on a build define, so a plugin author needs
 * no flags from Oak's own build to compile against this header. */
#if defined(_WIN32)
#define OAK_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define OAK_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define OAK_PLUGIN_EXPORT
#endif

typedef const oak_plugin_t* (*oak_plugin_main_fn)(void);

#ifdef __cplusplus
}
#endif
