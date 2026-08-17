#include "oak_plugin_host.h"

#include "oak_version.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static int fail(char* err, const usize err_cap, const char* fmt, ...)
{
  if (err && err_cap > 0u)
  {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_cap, fmt, ap);
    va_end(ap);
  }
  return -1;
}

/* The major component of "1.2.3", or -1. */
static int major_of(const char* version)
{
  if (!version)
    return -1;
  int major = 0;
  usize i = 0;
  for (; version[i] >= '0' && version[i] <= '9'; ++i)
    major = major * 10 + (version[i] - '0');
  return i == 0u ? -1 : major;
}

static void* open_library(const char* path)
{
#if defined(_WIN32)
  return (void*)LoadLibraryA(path);
#else
  /* RTLD_NOW so a missing symbol is a load failure with a name attached rather
   * than a crash at the first call. RTLD_LOCAL so a plugin's own dependencies
   * do not leak into the global namespace and collide with another plugin's. */
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void* find_symbol(void* handle, const char* name)
{
#if defined(_WIN32)
  /* The cast through a function pointer is what GetProcAddress returns; going
   * straight to void* is the conversion MSVC and GCC both warn about. */
  return (void*)(void (*)(void))GetProcAddress((HMODULE)handle, name);
#else
  return dlsym(handle, name);
#endif
}

static void close_library(void* handle)
{
#if defined(_WIN32)
  FreeLibrary((HMODULE)handle);
#else
  dlclose(handle);
#endif
}

static const char* load_error(void)
{
#if defined(_WIN32)
  return "the file is missing, is not a library, or needs a DLL that is not "
         "beside it";
#else
  const char* msg = dlerror();
  return msg ? msg : "unknown error";
#endif
}

int oak_plugin_host_load(const char* path,
                         const char* package,
                         oak_plugin_lib_t* out,
                         char* err,
                         const usize err_cap)
{
  if (!path || !out)
    return -1;
  memset(out, 0, sizeof *out);

  void* handle = open_library(path);
  if (!handle)
    return fail(err, err_cap, "cannot load '%s': %s", path, load_error());

  const oak_plugin_main_fn entry =
      (oak_plugin_main_fn)find_symbol(handle, OAK_PLUGIN_ENTRY);
  if (!entry)
  {
    close_library(handle);
    return fail(err, err_cap,
                "'%s' exports no %s; it is not an Oak plugin", path,
                OAK_PLUGIN_ENTRY);
  }

  const oak_plugin_t* plugin = entry();
  if (!plugin)
  {
    close_library(handle);
    return fail(err, err_cap, "'%s' declined to describe itself", path);
  }

  /* Check the ABI before touching any other field: everything below this line
   * assumes the struct is laid out the way this build understands it. */
  if (plugin->abi != OAK_PLUGIN_ABI)
  {
    const u32 got = plugin->abi;
    close_library(handle);
    return fail(err, err_cap,
                "'%s' was built for plugin ABI %u, but this oak speaks %u; "
                "it needs rebuilding",
                path, (unsigned)got, (unsigned)OAK_PLUGIN_ABI);
  }

  const int their_major = major_of(plugin->oak_version);
  const int our_major = major_of(OAK_VERSION_STRING);
  if (their_major < 0 || their_major != our_major)
  {
    /* Copied out first: every string the plugin owns lives in its own image,
     * so unloading the library invalidates it. */
    char their_version[64];
    snprintf(their_version, sizeof their_version, "%s",
             plugin->oak_version ? plugin->oak_version : "?");
    close_library(handle);
    return fail(err, err_cap,
                "'%s' was built against oak %s, and this is oak %s; a major "
                "version apart, the binding layout is not the same",
                path, their_version, OAK_VERSION_STRING);
  }

  if (!plugin->bind)
  {
    close_library(handle);
    return fail(err, err_cap, "'%s' has no bind function", path);
  }

  /* A library that thinks it is a different package than the one that cached
   * it means the wrong artifact was published; guessing which is right is not
   * this layer's call. */
  if (package && plugin->name && strcmp(package, plugin->name) != 0)
  {
    char claimed[128];
    snprintf(claimed, sizeof claimed, "%s", plugin->name);
    close_library(handle);
    return fail(err, err_cap,
                "package '%s' ships a library that says it is '%s'", package,
                claimed);
  }

  out->handle = handle;
  out->plugin = plugin;
  return 0;
}

void oak_plugin_host_unload(oak_plugin_lib_t* lib)
{
  if (!lib || !lib->handle)
    return;
  close_library(lib->handle);
  lib->handle = OAK_NULL;
  lib->plugin = OAK_NULL;
}
