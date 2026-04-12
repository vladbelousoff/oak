#include "oak_file_map.h"

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

int oak_file_map(const char* path, struct oak_file_map_t* out)
{
  out->data = NULL;
  out->size = 0;
#if defined(_WIN32)
  out->mapping_handle = NULL;
#else
  out->map_length = 0;
#endif

#if defined(_WIN32)
  const HANDLE file_handle =
      CreateFileA(path,
                  GENERIC_READ,
                  FILE_SHARE_READ,
                  NULL,
                  OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL,
                  NULL);
  if (file_handle == INVALID_HANDLE_VALUE)
  {
    fprintf(stderr, "error: could not open file '%s'\n", path);
    return -1;
  }

  LARGE_INTEGER file_size_li;
  if (!GetFileSizeEx(file_handle, &file_size_li))
  {
    fprintf(stderr, "error: could not read size of file '%s'\n", path);
    CloseHandle(file_handle);
    return -1;
  }

  const size_t file_size = (size_t)file_size_li.QuadPart;

  if (file_size == 0)
  {
    CloseHandle(file_handle);
    return 0;
  }

  const HANDLE mapping = CreateFileMappingA(
      file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
  CloseHandle(file_handle);
  if (!mapping)
  {
    fprintf(stderr, "error: could not map file '%s'\n", path);
    return -1;
  }

  char* view = (char*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (!view)
  {
    fprintf(stderr, "error: could not map file '%s'\n", path);
    CloseHandle(mapping);
    return -1;
  }

  out->data = view;
  out->size = file_size;
  out->mapping_handle = mapping;
  return 0;

#else

  const int fd = open(path, O_RDONLY);
  if (fd < 0)
  {
    fprintf(stderr, "error: could not open file '%s'\n", path);
    return -1;
  }

  struct stat st;
  if (fstat(fd, &st) != 0)
  {
    fprintf(stderr, "error: could not stat file '%s'\n", path);
    close(fd);
    return -1;
  }

  const size_t file_size = (size_t)st.st_size;

  if (file_size == 0)
  {
    close(fd);
    return 0;
  }

  void* mapped =
      mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);

  if (mapped == MAP_FAILED)
  {
    fprintf(stderr, "error: could not map file '%s'\n", path);
    return -1;
  }

  out->data = mapped;
  out->size = file_size;
  out->map_length = file_size;
  return 0;
#endif
}

void oak_file_unmap(struct oak_file_map_t* map)
{
  if (!map || !map->data)
    return;

#if defined(_WIN32)
  UnmapViewOfFile(map->data);
  CloseHandle(map->mapping_handle);
  map->mapping_handle = NULL;
#else
  munmap(map->data, map->map_length);
  map->map_length = 0;
#endif

  map->data = NULL;
  map->size = 0;
}
