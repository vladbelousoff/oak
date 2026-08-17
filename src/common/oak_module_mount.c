#include "oak_module_mount.h"

#include "internal/oak_module_loader.h"

/* Windows path comparison is case-insensitive, and a package cache path that
 * differs only in case from an importing module's path is a scope match there
 * and not on POSIX. Getting this wrong makes a mount silently invisible, so it
 * is spelled out rather than left to strcmp. */
static int path_char_eq(const char a, const char b)
{
#if defined(_WIN32)
  const char la = (a >= 'A' && a <= 'Z') ? (char)(a - 'A' + 'a') : a;
  const char lb = (b >= 'A' && b <= 'Z') ? (char)(b - 'A' + 'a') : b;
  return (la == lb) || ((la == '/' || la == '\\') && (lb == '/' || lb == '\\'));
#else
  return a == b;
#endif
}

/* Non-zero when `dir` is `prefix` itself or lies underneath it. Compares whole
 * path components, so "/a/bc" is not under "/a/b". */
static int path_is_under(const char* dir, const char* prefix)
{
  usize i = 0;
  for (; prefix[i]; ++i)
  {
    if (!dir[i] || !path_char_eq(dir[i], prefix[i]))
      return 0;
  }
  /* Trailing separator on the prefix already ended it at a boundary. */
  if (i > 0 && (prefix[i - 1u] == '/' || prefix[i - 1u] == '\\'))
    return 1;
  return dir[i] == 0 || dir[i] == '/' || dir[i] == '\\';
}

const char* oak_module_mount_find(const oak_container_t* mounts,
                                  const char* importer_dir,
                                  const char* ns,
                                  const usize ns_len,
                                  const char** out_package)
{
  if (out_package)
    *out_package = OAK_NULL;
  if (!mounts || !ns || ns_len == 0u)
    return OAK_NULL;

  const oak_module_mount_t* entries = OAK_CDATA(oak_module_mount_t, mounts);
  const oak_module_mount_t* best = OAK_NULL;
  usize best_scope_len = 0;

  for (usize i = 0; i < oak_size(mounts); ++i)
  {
    const oak_module_mount_t* m = &entries[i];
    if (strncmp(m->ns, ns, ns_len) != 0 || m->ns[ns_len] != 0)
      continue;

    usize scope_len = 0;
    if (m->scope_root)
    {
      if (!importer_dir || !path_is_under(importer_dir, m->scope_root))
        continue;
      scope_len = strlen(m->scope_root);
    }

    /* Longest scope wins: a package's own dependency shadows an enclosing
     * one's rather than merging with it. A program-wide mount (scope_len 0)
     * is therefore the weakest, which is what makes it a default. */
    if (!best || scope_len > best_scope_len)
    {
      best = m;
      best_scope_len = scope_len;
    }
  }

  if (!best)
    return OAK_NULL;
  if (out_package)
    *out_package = best->package;
  return best->root_dir;
}

void oak_module_mounts_free(oak_allocator_t* a, oak_container_t* mounts)
{
  if (!mounts)
    return;
  oak_module_mount_t* entries = OAK_DATA(oak_module_mount_t, mounts);
  for (usize i = 0; i < oak_size(mounts); ++i)
  {
    oak_free(a, entries[i].scope_root, OAK_HERE);
    oak_free(a, entries[i].ns, OAK_HERE);
    oak_free(a, entries[i].root_dir, OAK_HERE);
    oak_free(a, entries[i].package, OAK_HERE);
  }
  oak_destroy(mounts);
}

/* Duplicate `s` with `a`, blaming `loc` -- the caller's site, since this runs
 * on behalf of whoever asked for the mount. Null in, null out. */
static char* mount_strdup(oak_allocator_t* a,
                          const char* s,
                          const oak_source_loc_t loc)
{
  if (!s)
    return OAK_NULL;
  const usize n = strlen(s) + 1u;
  char* copy = oak_alloc(a, n, loc);
  if (copy)
    memcpy(copy, s, n);
  return copy;
}

/* Report a rejected mount the same way a rejected binding is reported: append
 * to opts->bind_errors so oak_compile_ex surfaces it as a diagnostic rather
 * than the program failing later at an import that looks fine. */
static int mount_reject(oak_compile_options_t* opts, const char* fmt, ...)
{
  if (!opts || !opts->bind_errors)
    return -1;

  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  char* copy = mount_strdup(opts->allocator, buf, OAK_HERE);
  if (!copy)
    return -1;
  OAK_ASSERT(oak_push_back(opts->bind_errors, &copy));
  return -1;
}

int oak_module_loader_mount(oak_compile_options_t* opts,
                            const char* scope_root,
                            const char* ns,
                            const char* root_dir,
                            const char* label)
{
  if (!opts || !opts->module_mounts)
    return -1;
  if (!ns || !ns[0])
    return mount_reject(opts, "a module mount has no namespace");
  if (!root_dir || !root_dir[0])
    return mount_reject(opts, "module mount '%s' has no root directory", ns);
  if (strchr(ns, '.'))
    return mount_reject(opts,
                        "module mount '%s' must name a single segment, not a "
                        "dotted path",
                        ns);

  /* A mount over a built-in native module would let a package impersonate the
   * stdlib -- 'io' resolving to a downloaded directory instead of the trusted
   * one. The stdlib's own resolution deliberately outranks module-relative
   * files for exactly this reason; mounts must not be a way around it. */
  if (opts_has_native_module(opts, ns))
    return mount_reject(opts,
                        "cannot mount '%s': it is a built-in native module",
                        ns);

  oak_module_mount_t entry;
  entry.scope_root = scope_root ? path_canonicalize(opts->allocator, scope_root)
                                : OAK_NULL;
  entry.ns = mount_strdup(opts->allocator, ns, OAK_HERE);
  entry.root_dir = path_canonicalize(opts->allocator, root_dir);
  entry.package = mount_strdup(opts->allocator, label, OAK_HERE);

  if (!entry.ns || !entry.root_dir || (scope_root && !entry.scope_root) ||
      (label && !entry.package))
  {
    oak_free(opts->allocator, entry.scope_root, OAK_HERE);
    oak_free(opts->allocator, entry.ns, OAK_HERE);
    oak_free(opts->allocator, entry.root_dir, OAK_HERE);
    oak_free(opts->allocator, entry.package, OAK_HERE);
    return -1;
  }

  OAK_ASSERT(oak_push_back(opts->module_mounts, &entry));
  return 0;
}
