#pragma once

/*
 * Loading a native package's shared library.
 *
 * Private, because nothing an embedder does should require reaching for it:
 * plugins arrive through a package set, and the set owns their lifetime.
 */

#include "oak_allocator.h"
#include "oak_plugin.h"
#include "oak_types.h"

typedef struct oak_plugin_lib oak_plugin_lib_t;
struct oak_plugin_lib
{
  /* HMODULE or void* from dlopen. */
  void* handle;
  /* Borrowed from the library; valid until it is unloaded. */
  const oak_plugin_t* plugin;
};

/* Open `path`, find its entry point, and check that it is loadable here.
 *
 * `package` is the name the manifest says this library belongs to, and it must
 * match what the library says about itself -- a mismatch means the wrong
 * artifact was published or the wrong file was cached, and calling into it
 * would be a guess.  Returns 0, or -1 with a reason in `err`; nothing is left
 * loaded on failure. */
int oak_plugin_host_load(const char* path,
                         const char* package,
                         oak_plugin_lib_t* out,
                         char* err,
                         usize err_cap);

void oak_plugin_host_unload(oak_plugin_lib_t* lib);
