#include "oak_pkg_fs.h"

#include "internal/oak_pkg_util.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define OAK_FS_SEP '\\'
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define OAK_FS_SEP '/'
#endif

static int is_sep(const char c)
{
  return c == '/' || c == '\\';
}

int oak_pkg_is_dir(const char* path)
{
  if (!path || !path[0])
    return 0;
#if defined(_WIN32)
  const DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES &&
         (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static int mkdir_one(const char* path)
{
#if defined(_WIN32)
  if (_mkdir(path) == 0)
    return 0;
#else
  if (mkdir(path, 0777) == 0)
    return 0;
#endif
  /* Losing a race to create it is the same outcome as creating it. */
  return oak_pkg_is_dir(path) ? 0 : -1;
}

int oak_pkg_mkdir_p(const char* path)
{
  if (!path || !path[0])
    return -1;
  if (oak_pkg_is_dir(path))
    return 0;

  const usize len = strlen(path);
  if (len >= 4096u)
    return -1;

  char buf[4096];
  memcpy(buf, path, len + 1u);

  /* Walk forwards creating each prefix. Start past any root ("C:\", "/", or a
   * UNC "\\server\share") so the loop never tries to create one. */
  usize i = 0;
#if defined(_WIN32)
  if (len >= 2u && buf[1] == ':')
    i = 2u;
  else if (len >= 2u && is_sep(buf[0]) && is_sep(buf[1]))
  {
    i = 2u;
    int seen = 0;
    while (i < len && seen < 2)
    {
      if (is_sep(buf[i]))
        ++seen;
      ++i;
    }
  }
#endif
  while (i < len && is_sep(buf[i]))
    ++i;

  for (; i <= len; ++i)
  {
    if (i != len && !is_sep(buf[i]))
      continue;
    const char saved = buf[i];
    buf[i] = 0;
    if (buf[0] && !oak_pkg_is_dir(buf) && mkdir_one(buf) != 0)
      return -1;
    buf[i] = saved;
  }
  return oak_pkg_is_dir(path) ? 0 : -1;
}

oak_container_t* oak_pkg_list_dir(oak_allocator_t* a, const char* path)
{
  if (!a || !path)
    return OAK_NULL;
  oak_container_t* out = oak_vector_new(a, sizeof(char*));
  if (!out)
    return OAK_NULL;

#if defined(_WIN32)
  const usize plen = strlen(path);
  char* pattern = oak_alloc(a, plen + 3u, OAK_HERE);
  if (!pattern)
  {
    oak_destroy(out);
    return OAK_NULL;
  }
  memcpy(pattern, path, plen);
  pattern[plen] = OAK_FS_SEP;
  pattern[plen + 1u] = '*';
  pattern[plen + 2u] = 0;

  WIN32_FIND_DATAA fd;
  const HANDLE find = FindFirstFileA(pattern, &fd);
  oak_free(a, pattern, OAK_HERE);
  if (find == INVALID_HANDLE_VALUE)
  {
    oak_destroy(out);
    return OAK_NULL;
  }
  do
  {
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
      continue;
    char* name = oak_pkg_strdup(a, fd.cFileName, OAK_HERE);
    if (!name || !oak_push_back(out, &name))
    {
      oak_free(a, name, OAK_HERE);
      break;
    }
  } while (FindNextFileA(find, &fd));
  FindClose(find);
#else
  DIR* dir = opendir(path);
  if (!dir)
  {
    oak_destroy(out);
    return OAK_NULL;
  }
  const struct dirent* ent;
  while ((ent = readdir(dir)) != OAK_NULL)
  {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    char* name = oak_pkg_strdup(a, ent->d_name, OAK_HERE);
    if (!name || !oak_push_back(out, &name))
    {
      oak_free(a, name, OAK_HERE);
      break;
    }
  }
  closedir(dir);
#endif
  return out;
}

void oak_pkg_list_free(oak_allocator_t* a, oak_container_t* list)
{
  if (!a || !list)
    return;
  char** names = OAK_DATA(char*, list);
  for (usize i = 0; i < oak_size(list); ++i)
    oak_free(a, names[i], OAK_HERE);
  oak_destroy(list);
}

int oak_pkg_rmtree(oak_allocator_t* a, const char* path)
{
  if (!a || !path || !path[0])
    return -1;
  if (!oak_pkg_is_dir(path))
  {
#if defined(_WIN32)
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
      return 0;
    /* A read-only file refuses to be deleted until the attribute is cleared,
     * which is the usual state of everything in a git checkout's object store
     * on Windows. */
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    return DeleteFileA(path) ? 0 : -1;
#else
    if (access(path, F_OK) != 0)
      return 0;
    return unlink(path) == 0 ? 0 : -1;
#endif
  }

  int rc = 0;

#if defined(_WIN32)
  const usize plen = strlen(path);
  char* pattern = oak_alloc(a, plen + 3u, OAK_HERE);
  if (!pattern)
    return -1;
  memcpy(pattern, path, plen);
  pattern[plen] = OAK_FS_SEP;
  pattern[plen + 1u] = '*';
  pattern[plen + 2u] = 0;

  WIN32_FIND_DATAA fd;
  const HANDLE find = FindFirstFileA(pattern, &fd);
  oak_free(a, pattern, OAK_HERE);
  if (find != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
        continue;
      const usize nlen = strlen(fd.cFileName);
      char* child = oak_alloc(a, plen + nlen + 2u, OAK_HERE);
      if (!child)
      {
        rc = -1;
        break;
      }
      memcpy(child, path, plen);
      child[plen] = OAK_FS_SEP;
      memcpy(child + plen + 1u, fd.cFileName, nlen + 1u);
      if (oak_pkg_rmtree(a, child) != 0)
        rc = -1;
      oak_free(a, child, OAK_HERE);
    } while (FindNextFileA(find, &fd));
    FindClose(find);
  }
  SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
  if (!RemoveDirectoryA(path))
    rc = -1;
#else
  DIR* dir = opendir(path);
  if (!dir)
    return -1;
  const usize plen = strlen(path);
  const struct dirent* ent;
  while ((ent = readdir(dir)) != OAK_NULL)
  {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    const usize nlen = strlen(ent->d_name);
    char* child = oak_alloc(a, plen + nlen + 2u, OAK_HERE);
    if (!child)
    {
      rc = -1;
      break;
    }
    memcpy(child, path, plen);
    child[plen] = OAK_FS_SEP;
    memcpy(child + plen + 1u, ent->d_name, nlen + 1u);
    if (oak_pkg_rmtree(a, child) != 0)
      rc = -1;
    oak_free(a, child, OAK_HERE);
  }
  closedir(dir);
  if (rmdir(path) != 0)
    rc = -1;
#endif
  return rc;
}

int oak_pkg_rename(const char* from, const char* to)
{
  if (!from || !to)
    return -1;
#if defined(_WIN32)
  /* MoveFile fails rather than merging when the destination exists, which is
   * what we want: another process finishing the same fetch first is success,
   * not a reason to overwrite a tree someone may already be reading. */
  if (MoveFileA(from, to))
    return 0;
  return oak_pkg_is_dir(to) ? 0 : -1;
#else
  if (rename(from, to) == 0)
    return 0;
  return oak_pkg_is_dir(to) ? 0 : -1;
#endif
}

char* oak_pkg_stage_path(oak_allocator_t* a,
                         const char* parent,
                         const oak_source_loc_t loc)
{
  if (!a || !parent)
    return OAK_NULL;

#if defined(_WIN32)
  const unsigned long id = (unsigned long)GetCurrentProcessId();
#else
  const unsigned long id = (unsigned long)getpid();
#endif

  const usize len = strlen(parent);
  const usize cap = len + 40u;
  for (unsigned attempt = 0; attempt < 64u; ++attempt)
  {
    char* out = oak_alloc(a, cap, loc);
    if (!out)
      return OAK_NULL;
    snprintf(out, cap, "%s%cstage-%lu-%u", parent, OAK_FS_SEP, id, attempt);
    if (!oak_pkg_is_dir(out))
      return out;
    oak_free(a, out, OAK_HERE);
  }
  return OAK_NULL;
}

int oak_pkg_write_file(const char* path, const void* data, const usize len)
{
  if (!path)
    return -1;
  FILE* f = fopen(path, "wb");
  if (!f)
    return -1;
  const int ok = (len == 0u) || (fwrite(data, 1u, len, f) == len);
  /* Report a close failure too: a buffered write that fails on flush would
   * otherwise leave a truncated manifest behind and report success. */
  return (fclose(f) == 0 && ok) ? 0 : -1;
}
