#include "internal/oak_module_loader.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdint.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

char* path_dirname_dup(oak_allocator_t* a, const char* path)
{
  const char* last = OAK_NULL;
  for (const char* p = path; *p; ++p)
  {
    if (*p == '/' || *p == '\\')
      last = p;
  }
  if (!last)
  {
    char* dot = oak_alloc(a, 2u, OAK_HERE);
    dot[0] = '.';
    dot[1] = 0;
    return dot;
  }
  const usize n = (usize)(last - path);
  char* d = oak_alloc(a, n + 1u, OAK_HERE);
  memcpy(d, path, n);
  d[n] = 0;
  return d;
}

char* path_resolve_dotted(oak_allocator_t* a,
                          const char* base_dir,
                          const char* dotted)
{
  const usize bdlen = strlen(base_dir);
  const usize dlen = strlen(dotted);
  const usize total = bdlen + 1u + dlen + 4u + 1u;
  char* out = oak_alloc(a, total, OAK_HERE);
  usize w = 0;
  memcpy(out + w, base_dir, bdlen);
  w += bdlen;
  if (bdlen == 0u || (out[bdlen - 1u] != '/' && out[bdlen - 1u] != '\\'))
    out[w++] = OAK_PATH_SEP;
  for (usize i = 0; i < dlen; ++i)
    out[w++] = (dotted[i] == '.') ? OAK_PATH_SEP : dotted[i];
  memcpy(out + w, ".oak", 4u);
  w += 4u;
  out[w] = 0;
  return out;
}

char* path_join(oak_allocator_t* a, const char* base, const char* rel)
{
  const usize bl = strlen(base);
  const usize rl = strlen(rel);
  char* out = oak_alloc(a, bl + 1u + rl + 1u, OAK_HERE);
  usize w = 0;
  memcpy(out + w, base, bl);
  w += bl;
  if (bl != 0u && out[bl - 1u] != '/' && out[bl - 1u] != '\\')
    out[w++] = OAK_PATH_SEP;
  memcpy(out + w, rel, rl);
  w += rl;
  out[w] = 0;
  return out;
}

char* path_executable_dir(oak_allocator_t* a)
{
  /* Use the OS narrow-char API so the result matches the encoding fopen()
   * expects elsewhere in this file (path_exists). 4096 covers realistic
   * install paths; treat truncation as "unknown". */
  char buf[4096];
  buf[0] = 0;
#if defined(_WIN32)
  const DWORD n = GetModuleFileNameA(OAK_NULL, buf, (DWORD)sizeof(buf));
  if (n == 0u || n >= (DWORD)sizeof(buf))
    return OAK_NULL;
#elif defined(__APPLE__)
  uint32_t size = (uint32_t)sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0)
    return OAK_NULL;
#else
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1u);
  if (n <= 0 || (usize)n >= sizeof(buf))
    return OAK_NULL;
  buf[n] = 0;
#endif
  return path_dirname_dup(a, buf);
}

char* path_canonicalize(oak_allocator_t* a, const char* path)
{
#if defined(_WIN32)
  char* abs = _fullpath(OAK_NULL, path, 0);
  if (abs)
  {
    const usize n = strlen(abs);
    char* copy = oak_alloc(a, n + 1u, OAK_HERE);
    memcpy(copy, abs, n + 1u);
    free(abs);
    return copy;
  }
#else
  char buf[PATH_MAX];
  if (realpath(path, buf))
  {
    const usize n = strlen(buf);
    char* copy = oak_alloc(a, n + 1u, OAK_HERE);
    memcpy(copy, buf, n + 1u);
    return copy;
  }
#endif
  const usize n = strlen(path);
  char* copy = oak_alloc(a, n + 1u, OAK_HERE);
  memcpy(copy, path, n + 1u);
  return copy;
}

int path_exists(const char* path)
{
  FILE* f = fopen(path, "rb");
  if (!f)
    return 0;
  fclose(f);
  return 1;
}

int path_dir_exists(const char* path)
{
#if defined(_WIN32)
  const DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES &&
         (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

char* dotted_name_from_path(oak_allocator_t* a,
                            const oak_ast_node_t* path_node)
{
  usize total = 0;
  int count = 0;
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &path_node->children)
  {
    const oak_ast_node_t* ident =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    total += oak_token_size(ident->token);
    ++count;
  }
  if (count == 0)
  {
    char* empty = oak_alloc(a, 1u, OAK_HERE);
    empty[0] = 0;
    return empty;
  }
  total += (usize)(count - 1);
  char* buf = oak_alloc(a, total + 1u, OAK_HERE);
  usize w = 0;
  int first = 1;
  OAK_LIST_FOR_EACH(pos, &path_node->children)
  {
    const oak_ast_node_t* ident =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    if (!first)
      buf[w++] = '.';
    const int len = oak_token_size(ident->token);
    memcpy(buf + w, oak_token_text(ident->token), len);
    w += len;
    first = 0;
  }
  buf[w] = 0;
  return buf;
}
