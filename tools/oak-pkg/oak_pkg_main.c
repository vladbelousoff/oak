/*
 * oak-pkg -- resolve, fetch and record a project's dependencies.
 *
 * A separate binary from `oak` on purpose.  Running a program must never be
 * the thing that reaches the network or decides a version; by the time `oak`
 * starts, every such question has been answered and written into oak.lock.
 * Keeping the two apart is what makes that guarantee structural rather than a
 * promise.
 */

#include "oak_pkg_tool.h"

#include "internal/oak_pkg_util.h"

#include "oak_pkg_cache.h"
#include "oak_version.h"

#include <mimalloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#define MANIFEST_NAME "oak.json"
#define LOCK_NAME "oak.lock"
#define SEARCH_DEPTH 32

static void usage(FILE* out)
{
  fprintf(out,
          "oak-pkg -- the Oak package manager\n"
          "\n"
          "usage: oak-pkg <command> [options]\n"
          "\n"
          "commands:\n"
          "  init [--name <owner/pkg>] [--src <dir>]\n"
          "                       start a project in this directory\n"
          "  add <spec> [--as <alias>] [--tag <t>] [--rev <r>]\n"
          "      [--sha256 <hash>] [--strip <n>]\n"
          "                       add a dependency and install it\n"
          "  remove <alias>       drop a dependency\n"
          "  install              fetch what oak.json asks for, write oak.lock\n"
          "  update [<alias>]     re-resolve, taking newer versions\n"
          "  tree                 show the resolved dependency graph\n"
          "  check                verify the lock, the cache and any plugins\n"
          "  cache path|clean     where downloads live, or remove them\n"
          "\n"
          "options:\n"
          "  --quiet              only report problems\n"
          "  --version            print the version and platform\n"
          "  --help               print this\n"
          "\n"
          "specs:\n"
          "  github:owner/repo@1.2.0     gitlab:owner/repo@1.2.0\n"
          "  https://host/repo.git --tag v1.2.0\n"
          "  https://host/pkg-1.2.0.tar.gz [--sha256 <hash>]\n"
          "  ./relative/path\n"
          "\n"
          "environment:\n"
          "  OAK_PACKAGE_CACHE    where fetched packages are kept\n"
          "  OAK_GIT              the git binary to run\n"
          "  OAK_OFFLINE          refuse to use the network\n");
}

void oak_pkg_say(const oak_pkg_tool_t* t, const char* fmt, ...)
{
  if (t && t->quiet)
    return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

/* First directory at or above `start` holding a manifest, or null. */
static char* find_project(oak_allocator_t* a, const char* start)
{
  char* dir = oak_pkg_strdup(a, start, OAK_HERE);
  if (!dir)
    return OAK_NULL;

  for (int depth = 0; depth < SEARCH_DEPTH; ++depth)
  {
    char* candidate = oak_pkg_path_join(a, dir, MANIFEST_NAME, OAK_HERE);
    if (!candidate)
      break;
    FILE* probe = fopen(candidate, "rb");
    oak_free(a, candidate, OAK_HERE);
    if (probe)
    {
      fclose(probe);
      return dir;
    }

    char* parent = oak_pkg_path_dirname(a, dir, OAK_HERE);
    if (!parent || strcmp(parent, dir) == 0)
    {
      oak_free(a, parent, OAK_HERE);
      break;
    }
    oak_free(a, dir, OAK_HERE);
    dir = parent;
  }

  oak_free(a, dir, OAK_HERE);
  return OAK_NULL;
}

int oak_pkg_tool_open(oak_pkg_tool_t* out, oak_allocator_t* a)
{
  memset(out, 0, sizeof *out);
  out->a = a;

  char cwd[4096];
  if (!getcwd(cwd, (int)sizeof cwd))
  {
    fprintf(stderr, "oak-pkg: cannot determine the current directory\n");
    return -1;
  }

  /* No manifest above us is not an error here: `init` is how one gets made,
   * and every other command reports the absence itself, in its own words. */
  char* found = find_project(a, cwd);
  out->project_dir = found ? found : oak_pkg_strdup(a, cwd, OAK_HERE);
  if (!out->project_dir)
    return -1;

  out->manifest_path =
      oak_pkg_path_join(a, out->project_dir, MANIFEST_NAME, OAK_HERE);
  out->lock_path = oak_pkg_path_join(a, out->project_dir, LOCK_NAME, OAK_HERE);
  out->cache_root = oak_pkg_cache_root(a, OAK_HERE);
  if (!out->manifest_path || !out->lock_path)
    return -1;

  const char* offline = getenv("OAK_OFFLINE");
  out->offline = offline && offline[0] && strcmp(offline, "0") != 0;

  char reason[OAK_PKG_ERROR_MAX] = { 0 };
  if (oak_pkg_lock_read(&out->lock, a, out->lock_path, reason, sizeof reason) ==
      0)
    out->have_lock = 1;
  else
    /* A lock this build cannot read is not fatal to oak-pkg -- regenerating it
     * is precisely what `install` is for -- so note it and carry on. */
    fprintf(stderr, "oak-pkg: ignoring the existing lockfile: %s\n", reason);

  return 0;
}

void oak_pkg_tool_close(oak_pkg_tool_t* t)
{
  if (!t || !t->a)
    return;
  oak_pkg_tool_forget(t);
  oak_pkg_lock_free(&t->lock);
  oak_free(t->a, t->project_dir, OAK_HERE);
  oak_free(t->a, t->manifest_path, OAK_HERE);
  oak_free(t->a, t->lock_path, OAK_HERE);
  oak_free(t->a, t->cache_root, OAK_HERE);
  memset(t, 0, sizeof *t);
}

int main(const int argc, const char* argv[])
{
  if (argc < 2)
  {
    usage(stderr);
    return 1;
  }

  /* Global flags are accepted before or after the command, because typing
   * `oak-pkg install --quiet` is the natural order and refusing it teaches
   * nothing. */
  int quiet = 0;
  const char* command = OAK_NULL;
  const char* rest[64];
  int rest_count = 0;

  for (int i = 1; i < argc; ++i)
  {
    const char* arg = argv[i];
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
    {
      usage(stdout);
      return 0;
    }
    if (strcmp(arg, "--version") == 0)
    {
      printf("oak-pkg %s (%s)\n", oak_version(), oak_platform());
      return 0;
    }
    if (strcmp(arg, "--quiet") == 0)
    {
      quiet = 1;
      continue;
    }
    if (arg[0] == '-' && arg[1] != '\0' && arg[1] != '-')
    {
      fprintf(stderr,
              "oak-pkg: short options are not supported; use --long-option\n");
      return 1;
    }
    if (!command && arg[0] != '-')
    {
      command = arg;
      continue;
    }
    if (rest_count == (int)(sizeof rest / sizeof rest[0]))
    {
      fprintf(stderr, "oak-pkg: too many arguments\n");
      return 1;
    }
    rest[rest_count++] = arg;
  }

  if (!command)
  {
    usage(stderr);
    return 1;
  }

  oak_allocator_t allocator;
  oak_allocator_init(&allocator, mi_malloc, mi_realloc, mi_free);

  oak_pkg_tool_t tool;
  if (oak_pkg_tool_open(&tool, &allocator) != 0)
  {
    allocator.shutdown(&allocator);
    return 1;
  }
  tool.quiet = quiet;

  int rc;
  if (strcmp(command, "init") == 0)
    rc = oak_pkg_cmd_init(&tool, rest_count, rest);
  else if (strcmp(command, "add") == 0)
    rc = oak_pkg_cmd_add(&tool, rest_count, rest);
  else if (strcmp(command, "remove") == 0)
    rc = oak_pkg_cmd_remove(&tool, rest_count, rest);
  else if (strcmp(command, "install") == 0)
    rc = oak_pkg_cmd_install(&tool, rest_count, rest);
  else if (strcmp(command, "update") == 0)
    rc = oak_pkg_cmd_update(&tool, rest_count, rest);
  else if (strcmp(command, "tree") == 0)
    rc = oak_pkg_cmd_tree(&tool, rest_count, rest);
  else if (strcmp(command, "check") == 0)
    rc = oak_pkg_cmd_check(&tool, rest_count, rest);
  else if (strcmp(command, "cache") == 0)
    rc = oak_pkg_cmd_cache(&tool, rest_count, rest);
  else
  {
    fprintf(stderr, "oak-pkg: there is no '%s' command\n", command);
    usage(stderr);
    rc = 1;
  }

  oak_pkg_tool_close(&tool);
  allocator.shutdown(&allocator);
  return rc == 0 ? 0 : 1;
}
