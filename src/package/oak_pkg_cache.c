#include "oak_pkg_cache.h"

#include "internal/oak_pkg_util.h"

#include "oak_count_of.h"
#include "oak_version.h"

#include <stdlib.h>

#if defined(_WIN32)
#define OAK_PKG_SEP '\\'
#else
#define OAK_PKG_SEP '/'
#endif

/* Join `base` and `rel` with exactly one separator. */
static char* join(oak_allocator_t* a,
                  const char* base,
                  const char* rel,
                  const oak_source_loc_t loc)
{
  const usize bl = strlen(base);
  const usize rl = strlen(rel);
  char* out = oak_alloc(a, bl + rl + 2u, loc);
  if (!out)
    return OAK_NULL;
  memcpy(out, base, bl);
  usize w = bl;
  if (bl != 0u && out[bl - 1u] != '/' && out[bl - 1u] != '\\')
    out[w++] = OAK_PKG_SEP;
  memcpy(out + w, rel, rl);
  out[w + rl] = 0;
  return out;
}

/* The last path segment of a URL, with any ".git" or archive extension and any
 * query string removed, reduced to characters that are safe in a directory
 * name on every platform. Purely cosmetic -- the digest that follows it is what
 * makes the name unique -- so anything unusable simply becomes "pkg". */
static void readable_stem(const char* url, char* out, const usize cap)
{
  const char* end = url + strlen(url);
  for (const char* p = url; *p; ++p)
    if (*p == '?' || *p == '#')
    {
      end = p;
      break;
    }
  /* A URL ending in '/' has its name in the segment before it. */
  while (end > url && (end[-1] == '/' || end[-1] == '\\'))
    --end;

  const char* start = end;
  while (start > url && start[-1] != '/' && start[-1] != '\\')
    --start;

  usize n = 0;
  for (const char* p = start; p < end && n + 1u < cap; ++p)
  {
    const char c = *p;
    const int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_';
    out[n++] = safe ? c : '-';
  }
  out[n] = 0;

  /* Trim the extensions people write and nobody wants in a directory name. */
  static const char* const drops[] = { "-git", "-tar-gz", "-tar-xz",
                                       "-tar-zst", "-tgz", "-zip", "-tar" };
  int trimmed = 1;
  while (trimmed)
  {
    trimmed = 0;
    const usize len = strlen(out);
    for (usize i = 0; i < OAK_COUNT_OF(drops); ++i)
    {
      const usize dl = strlen(drops[i]);
      if (len > dl && strcmp(out + len - dl, drops[i]) == 0)
      {
        out[len - dl] = 0;
        trimmed = 1;
        break;
      }
    }
  }

  if (!out[0])
    memcpy(out, "pkg", 4u);
}

char* oak_pkg_cache_root(oak_allocator_t* a, const oak_source_loc_t loc)
{
  const char* override = getenv("OAK_PACKAGE_CACHE");
  if (override && override[0])
    return oak_pkg_strdup(a, override, loc);

#if defined(_WIN32)
  const char* home = getenv("USERPROFILE");
  if (!home || !home[0])
    home = getenv("LOCALAPPDATA");
#else
  const char* home = getenv("HOME");
#endif
  if (!home || !home[0])
    return OAK_NULL;

  char* oak_dir = join(a, home, ".oak", loc);
  if (!oak_dir)
    return OAK_NULL;
  char* out = join(a, oak_dir, "packages", loc);
  oak_free(a, oak_dir, OAK_HERE);
  return out;
}

char* oak_pkg_cache_dir(oak_allocator_t* a,
                        const char* root,
                        const oak_pkg_source_t* source,
                        const char* rev,
                        const char* sha256,
                        const oak_source_loc_t loc)
{
  if (!a || !root || !source)
    return OAK_NULL;

  char stem[64];
  char leaf[192];

  if (source->kind == OAK_PKG_SOURCE_GIT)
  {
    /* The commit, not the tag: a tag can be moved to different content, and a
     * cache entry whose meaning can change is not a cache entry. */
    if (!rev || !rev[0])
      return OAK_NULL;
    readable_stem(source->location, stem, sizeof stem);
    snprintf(leaf, sizeof leaf, "git%c%s-%s", OAK_PKG_SEP, stem, rev);
    return join(a, root, leaf, loc);
  }

  if (source->kind == OAK_PKG_SOURCE_URL)
  {
    if (!sha256 || !sha256[0])
      return OAK_NULL;
    readable_stem(source->location, stem, sizeof stem);
    snprintf(leaf, sizeof leaf, "archive%c%s-%s", OAK_PKG_SEP, stem, sha256);
    return join(a, root, leaf, loc);
  }

  /* A path dependency is already where it is going to be. */
  return OAK_NULL;
}

char* oak_pkg_native_lib_path(oak_allocator_t* a,
                              const char* package_dir,
                              const oak_pkg_native_t* native,
                              const oak_source_loc_t loc)
{
  if (!a || !package_dir || !native || !native->lib || !native->dir)
    return OAK_NULL;

  /* Each platform names shared libraries its own way, so the manifest gives
   * the base name once and the rest is derived. Listing four filenames per
   * package would be four chances to typo one. */
#if defined(_WIN32)
  const char* prefix = "";
  const char* suffix = ".dll";
#elif defined(__APPLE__)
  const char* prefix = "lib";
  const char* suffix = ".dylib";
#else
  const char* prefix = "lib";
  const char* suffix = ".so";
#endif

  char rel[320];
  snprintf(rel, sizeof rel, "%s%c%s%c%s%s%s", native->dir, OAK_PKG_SEP,
           OAK_PLATFORM, OAK_PKG_SEP, prefix, native->lib, suffix);
  return join(a, package_dir, rel, loc);
}
