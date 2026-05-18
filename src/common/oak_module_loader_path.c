#include "internal/oak_module_loader.h"

char* path_dirname_dup(struct oak_allocator_t* a, const char* path)
{
  const char* last = null;
  for (const char* p = path; *p; ++p)
  {
    if (*p == '/' || *p == '\\')
      last = p;
  }
  if (!last)
  {
    char* dot = OAK_ALLOC(a, 2u);
    dot[0] = '.';
    dot[1] = 0;
    return dot;
  }
  const usize n = (usize)(last - path);
  char* d = OAK_ALLOC(a, n + 1u);
  memcpy(d, path, n);
  d[n] = 0;
  return d;
}

char* path_resolve_dotted(struct oak_allocator_t* a,
                          const char* base_dir,
                          const char* dotted)
{
  const usize bdlen = strlen(base_dir);
  const usize dlen = strlen(dotted);
  const usize total = bdlen + 1u + dlen + 4u + 1u;
  char* out = OAK_ALLOC(a, total);
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

char* path_canonicalize(struct oak_allocator_t* a, const char* path)
{
#if defined(_WIN32)
  char* abs = _fullpath(null, path, 0);
  if (abs)
  {
    const usize n = strlen(abs);
    char* copy = OAK_ALLOC(a, n + 1u);
    memcpy(copy, abs, n + 1u);
    free(abs);
    return copy;
  }
#else
  char buf[PATH_MAX];
  if (realpath(path, buf))
  {
    const usize n = strlen(buf);
    char* copy = OAK_ALLOC(a, n + 1u);
    memcpy(copy, buf, n + 1u);
    return copy;
  }
#endif
  const usize n = strlen(path);
  char* copy = OAK_ALLOC(a, n + 1u);
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

char* dotted_name_from_path(struct oak_allocator_t* a,
                            const struct oak_ast_node_t* path_node)
{
  usize total = 0;
  int count = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &path_node->children)
  {
    const struct oak_ast_node_t* ident =
        oak_container_of(pos, struct oak_ast_node_t, link);
    total += oak_token_length(ident->token);
    ++count;
  }
  if (count == 0)
  {
    char* empty = OAK_ALLOC(a, 1u);
    empty[0] = 0;
    return empty;
  }
  total += (usize)(count - 1);
  char* buf = OAK_ALLOC(a, total + 1u);
  usize w = 0;
  int first = 1;
  oak_list_for_each(pos, &path_node->children)
  {
    const struct oak_ast_node_t* ident =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (!first)
      buf[w++] = '.';
    const usize len = oak_token_length(ident->token);
    memcpy(buf + w, oak_token_text(ident->token), len);
    w += len;
    first = 0;
  }
  buf[w] = 0;
  return buf;
}

const struct oak_ast_node_t* dotted_path_last_segment(
    const struct oak_ast_node_t* path_node)
{
  const struct oak_ast_node_t* last = null;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &path_node->children) last =
      oak_container_of(pos, struct oak_ast_node_t, link);
  return last;
}
