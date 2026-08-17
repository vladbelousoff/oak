#include "internal/oak_pkg_util.h"

#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#define OAK_PKG_PATH_SEP '\\'
#else
#include <limits.h>
#define OAK_PKG_PATH_SEP '/'
#endif

char* oak_pkg_strdup(oak_allocator_t* a, const char* s, const oak_source_loc_t loc)
{
  if (!s)
    return OAK_NULL;
  return oak_pkg_strndup(a, s, strlen(s), loc);
}

char* oak_pkg_strndup(oak_allocator_t* a,
                      const char* s,
                      const usize n,
                      const oak_source_loc_t loc)
{
  if (!s)
    return OAK_NULL;
  char* copy = oak_alloc(a, n + 1u, loc);
  if (!copy)
    return OAK_NULL;
  memcpy(copy, s, n);
  copy[n] = 0;
  return copy;
}

int oak_pkg_fail(char* err, const usize err_cap, const char* fmt, ...)
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

char* oak_pkg_read_file(oak_allocator_t* a,
                        const char* path,
                        usize* out_len,
                        char* err,
                        const usize err_cap,
                        const oak_source_loc_t loc)
{
  if (out_len)
    *out_len = 0;

  FILE* f = fopen(path, "rb");
  if (!f)
  {
    oak_pkg_fail(err, err_cap, "cannot open '%s'", path);
    return OAK_NULL;
  }

  if (fseek(f, 0, SEEK_END) != 0)
  {
    fclose(f);
    oak_pkg_fail(err, err_cap, "cannot size '%s'", path);
    return OAK_NULL;
  }
  const long size = ftell(f);
  if (size < 0)
  {
    fclose(f);
    oak_pkg_fail(err, err_cap, "cannot size '%s'", path);
    return OAK_NULL;
  }
  rewind(f);

  char* buf = oak_alloc(a, (usize)size + 1u, loc);
  if (!buf)
  {
    fclose(f);
    oak_pkg_fail(err, err_cap, "out of memory reading '%s'", path);
    return OAK_NULL;
  }

  /* A short read is a real failure here rather than a partial manifest: half a
   * JSON document parses as a syntax error and sends the author looking at
   * their own file. */
  const usize read = fread(buf, 1u, (usize)size, f);
  fclose(f);
  if (read != (usize)size)
  {
    oak_free(a, buf, OAK_HERE);
    oak_pkg_fail(err, err_cap, "short read from '%s'", path);
    return OAK_NULL;
  }

  buf[read] = 0;
  if (out_len)
    *out_len = read;
  return buf;
}

char* oak_pkg_path_join(oak_allocator_t* a,
                        const char* base,
                        const char* rel,
                        const oak_source_loc_t loc)
{
  if (!a || !base || !rel)
    return OAK_NULL;

  /* An absolute `rel` replaces `base` outright, which is what a manifest
   * writing an absolute path dependency means. */
  const int rel_absolute = rel[0] == '/' || rel[0] == '\\' ||
                           (rel[0] && rel[1] == ':');
  if (rel_absolute)
    return oak_pkg_strdup(a, rel, loc);

  const usize bl = strlen(base);
  const usize rl = strlen(rel);
  char* out = oak_alloc(a, bl + rl + 2u, loc);
  if (!out)
    return OAK_NULL;
  memcpy(out, base, bl);
  usize w = bl;
  if (bl != 0u && out[bl - 1u] != '/' && out[bl - 1u] != '\\')
    out[w++] = OAK_PKG_PATH_SEP;
  memcpy(out + w, rel, rl);
  out[w + rl] = 0;
  return out;
}

char* oak_pkg_path_abs(oak_allocator_t* a,
                       const char* path,
                       const oak_source_loc_t loc)
{
  if (!a || !path)
    return OAK_NULL;

#if defined(_WIN32)
  char buf[4096];
  const DWORD n = GetFullPathNameA(path, (DWORD)sizeof buf, buf, OAK_NULL);
  if (n == 0u || n >= sizeof buf)
    return OAK_NULL;
  /* GetFullPathName normalises separators and ".." but does not check that the
   * path exists, which is what callers rely on to report a missing dependency
   * by its resolved location rather than as a null. */
  return oak_pkg_strdup(a, buf, loc);
#else
  char buf[PATH_MAX];
  if (!realpath(path, buf))
    return OAK_NULL;
  return oak_pkg_strdup(a, buf, loc);
#endif
}

char* oak_pkg_path_dirname(oak_allocator_t* a,
                           const char* path,
                           const oak_source_loc_t loc)
{
  if (!a || !path)
    return OAK_NULL;
  const char* end = path + strlen(path);
  while (end > path && (end[-1] == '/' || end[-1] == '\\'))
    --end;
  while (end > path && end[-1] != '/' && end[-1] != '\\')
    --end;
  while (end > path + 1 && (end[-1] == '/' || end[-1] == '\\'))
    --end;
  if (end == path)
    return oak_pkg_strdup(a, ".", loc);
  return oak_pkg_strndup(a, path, (usize)(end - path), loc);
}
