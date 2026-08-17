/*
 * The oak-pkg commands.
 *
 * Every command that changes something ends by writing oak.lock, because the
 * lock is the only artifact `oak` reads: a manifest edit that is not followed
 * by resolution has changed what the project asks for without changing what it
 * builds, and that gap is where "works on my machine" comes from.
 */

#include "oak_pkg_tool.h"

#include "internal/oak_pkg_util.h"

#include "oak_pkg_cache.h"
#include "oak_pkg_fs.h"
#include "oak_pkg_git.h"
#include "oak_pkg_resolve.h"
#include "oak_plugin.h"
#include "oak_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int complain(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "oak-pkg: ");
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  return -1;
}

/* Read the project's own manifest, or explain that there is not one. */
static int read_root(oak_pkg_tool_t* t, oak_pkg_manifest_t* out)
{
  char reason[OAK_PKG_ERROR_MAX] = { 0 };
  if (oak_pkg_manifest_read(out, t->a, t->manifest_path, reason,
                            sizeof reason) != 0)
  {
    if (!oak_pkg_is_dir(t->project_dir))
      return complain("%s", reason);
    FILE* probe = fopen(t->manifest_path, "rb");
    if (!probe)
      return complain("there is no oak.json here or above; run 'oak-pkg init'");
    fclose(probe);
    return complain("%s", reason);
  }
  return 0;
}

/* The value of `--name`, or null. Long options only, and always `--name value`
 * rather than `--name=value`, matching how `oak` itself reads its flags. */
static const char* option(const int argc,
                          const char* const* argv,
                          const char* name)
{
  for (int i = 0; i < argc - 1; ++i)
    if (strcmp(argv[i], name) == 0)
      return argv[i + 1];
  return OAK_NULL;
}

/* The first argument that is not an option or an option's value. */
static const char* positional(const int argc, const char* const* argv)
{
  for (int i = 0; i < argc; ++i)
  {
    if (argv[i][0] == '-')
    {
      ++i; /* skip its value */
      continue;
    }
    return argv[i];
  }
  return OAK_NULL;
}

/* Resolve the graph and write oak.lock. The one path every mutating command
 * funnels through, so the lock can never drift from the manifest. */
static int resolve_and_write(oak_pkg_tool_t* t, const oak_pkg_manifest_t* root)
{
  oak_pkg_lock_t lock;
  char reason[OAK_PKG_ERROR_MAX] = { 0 };

  if (oak_pkg_resolve(t->a, root, t->project_dir, oak_pkg_tool_fetch, t, &lock,
                      reason, sizeof reason) != 0)
    return complain("%s", reason);

  const usize count = oak_size(lock.entries);
  const int rc = oak_pkg_lock_write(&lock, t->lock_path, reason, sizeof reason);
  oak_pkg_lock_free(&lock);
  if (rc != 0)
    return complain("%s", reason);

  oak_pkg_say(t, "locked %zu package%s", count, count == 1u ? "" : "s");
  return 0;
}

int oak_pkg_cmd_init(oak_pkg_tool_t* t, const int argc, const char* const* argv)
{
  FILE* probe = fopen(t->manifest_path, "rb");
  if (probe)
  {
    fclose(probe);
    return complain("there is already an oak.json in %s", t->project_dir);
  }

  const char* name = option(argc, argv, "--name");
  const char* src = option(argc, argv, "--src");

  /* Default the name to the directory, which is right often enough to be
   * worth not asking, and easy to change when it is not. */
  char* fallback = OAK_NULL;
  if (!name)
  {
    const char* leaf = t->project_dir;
    for (const char* p = t->project_dir; *p; ++p)
      if (*p == '/' || *p == '\\')
        leaf = p + 1;
    const usize n = strlen(leaf) + 8u;
    fallback = oak_alloc(t->a, n, OAK_HERE);
    if (!fallback)
      return -1;
    snprintf(fallback, n, "me/%s", leaf[0] ? leaf : "package");
    name = fallback;
  }

  char reason[OAK_PKG_ERROR_MAX] = { 0 };
  const int rc = oak_pkg_manifest_init_file(t->a, t->manifest_path, name,
                                            OAK_NULL, src ? src : "src", reason,
                                            sizeof reason);
  oak_free(t->a, fallback, OAK_HERE);
  if (rc != 0)
    return complain("%s", reason);

  oak_pkg_say(t, "wrote %s", t->manifest_path);
  return 0;
}

int oak_pkg_cmd_add(oak_pkg_tool_t* t, const int argc, const char* const* argv)
{
  const char* spec = positional(argc, argv);
  if (!spec)
    return complain("add needs something to add, e.g. "
                    "'oak-pkg add github:acme/oak-json@1.2.0'");

  oak_pkg_manifest_t root;
  if (read_root(t, &root) != 0)
    return -1;

  char reason[OAK_PKG_ERROR_MAX] = { 0 };
  oak_pkg_source_t source;
  oak_semver_req_t req;
  int rc = oak_pkg_source_parse_spec(&source, &req, t->a, spec, reason,
                                     sizeof reason);
  if (rc != 0)
  {
    oak_pkg_source_free(t->a, &source);
    oak_pkg_manifest_free(&root);
    return complain("%s", reason);
  }

  /* Explicit pins override whatever the spec's shape implied. */
  const char* tag = option(argc, argv, "--tag");
  const char* rev = option(argc, argv, "--rev");
  const char* sha256 = option(argc, argv, "--sha256");
  const char* strip = option(argc, argv, "--strip");
  if (tag || rev)
  {
    oak_free(t->a, source.tag, OAK_HERE);
    oak_free(t->a, source.rev, OAK_HERE);
    source.tag = tag ? oak_pkg_strdup(t->a, tag, OAK_HERE) : OAK_NULL;
    source.rev = rev ? oak_pkg_strdup(t->a, rev, OAK_HERE) : OAK_NULL;
    source.kind = OAK_PKG_SOURCE_GIT;
  }
  if (sha256)
  {
    oak_free(t->a, source.sha256, OAK_HERE);
    source.sha256 = oak_pkg_strdup(t->a, sha256, OAK_HERE);
    source.kind = OAK_PKG_SOURCE_URL;
  }
  if (strip)
    source.strip = atoi(strip);

  if (source.kind == OAK_PKG_SOURCE_GIT && !source.tag && !source.rev)
  {
    oak_pkg_source_free(t->a, &source);
    oak_pkg_manifest_free(&root);
    return complain("'%s' needs a --tag or a --rev; an unpinned branch is not "
                    "reproducible",
                    spec);
  }

  /* Fetch before editing the manifest, so a typo leaves the project alone --
   * and so a URL with no digest gets one recorded rather than left open. */
  oak_pkg_fetched_t got;
  if (oak_pkg_tool_fetch(t, &source, &got, reason, sizeof reason) != 0)
  {
    oak_pkg_source_free(t->a, &source);
    oak_pkg_manifest_free(&root);
    return complain("%s", reason);
  }

  /* Reading the dependency's own manifest is what makes `add` more than a text
   * edit: it is where the alias, the version and the digest come from. */
  oak_pkg_manifest_t dep;
  const int have_dep = oak_pkg_manifest_read_dir(&dep, t->a, got.dir, spec,
                                                 reason, sizeof reason) == 0;
  rc = have_dep ? 0 : -1;

  const char* alias = option(argc, argv, "--as");
  char* alias_owned = OAK_NULL;
  if (rc == 0)
  {
    if (!alias)
      alias = alias_owned = oak_pkg_strdup(t->a, dep.module, OAK_HERE);
    /* Record the digest the fetch settled on, so the next one is verified. */
    if (source.kind == OAK_PKG_SOURCE_URL && !source.sha256 && got.sha256)
      source.sha256 = oak_pkg_strdup(t->a, got.sha256, OAK_HERE);
    rc = oak_pkg_manifest_add_dep(t->a, t->manifest_path, alias, &source, &req,
                                  reason, sizeof reason);
  }

  if (rc == 0)
  {
    char version[64];
    oak_semver_format(&dep.version, version, sizeof version);
    oak_pkg_say(t, "added %s %s as '%s'", dep.name, version, alias);
  }

  if (have_dep)
    oak_pkg_manifest_free(&dep);
  oak_free(t->a, alias_owned, OAK_HERE);
  oak_free(t->a, got.dir, OAK_HERE);
  oak_free(t->a, got.rev, OAK_HERE);
  oak_free(t->a, got.sha256, OAK_HERE);
  oak_pkg_source_free(t->a, &source);
  oak_pkg_manifest_free(&root);

  if (rc != 0)
    return complain("%s", reason);

  /* Re-read: the manifest on disk now has the new dependency in it. */
  oak_pkg_manifest_t updated;
  if (read_root(t, &updated) != 0)
    return -1;
  rc = resolve_and_write(t, &updated);
  oak_pkg_manifest_free(&updated);
  return rc;
}

int oak_pkg_cmd_remove(oak_pkg_tool_t* t,
                       const int argc,
                       const char* const* argv)
{
  const char* alias = positional(argc, argv);
  if (!alias)
    return complain("remove needs the name of a dependency");

  char reason[OAK_PKG_ERROR_MAX] = { 0 };
  if (oak_pkg_manifest_remove_dep(t->a, t->manifest_path, alias, reason,
                                  sizeof reason) != 0)
    return complain("%s", reason);
  oak_pkg_say(t, "removed '%s'", alias);

  oak_pkg_manifest_t root;
  if (read_root(t, &root) != 0)
    return -1;
  const int rc = resolve_and_write(t, &root);
  oak_pkg_manifest_free(&root);
  return rc;
}

int oak_pkg_cmd_install(oak_pkg_tool_t* t,
                        const int argc,
                        const char* const* argv)
{
  (void)argc;
  (void)argv;
  oak_pkg_manifest_t root;
  if (read_root(t, &root) != 0)
    return -1;
  const int rc = resolve_and_write(t, &root);
  oak_pkg_manifest_free(&root);
  return rc;
}

int oak_pkg_cmd_update(oak_pkg_tool_t* t,
                       const int argc,
                       const char* const* argv)
{
  (void)argc;
  (void)argv;
  oak_pkg_manifest_t root;
  if (read_root(t, &root) != 0)
    return -1;

  /* The difference between install and update in one line: stop honouring what
   * is already pinned, and take each tag as it stands today. */
  t->refresh = 1;
  const int rc = resolve_and_write(t, &root);
  oak_pkg_manifest_free(&root);
  return rc;
}

/* Print one package and everything below it. `depth` bounds a graph that a
 * malformed set of path dependencies could otherwise make cyclic. */
static void print_tree(oak_pkg_tool_t* t,
                       const oak_pkg_manifest_t* m,
                       const char* dir,
                       const int depth)
{
  if (depth > 24)
    return;

  const oak_pkg_dep_t* deps = OAK_CDATA(oak_pkg_dep_t, m->deps);
  for (usize i = 0; i < oak_size(m->deps); ++i)
  {
    for (int d = 0; d < depth; ++d)
      fputs("  ", stdout);
    fputs("- ", stdout);

    char* child_dir = OAK_NULL;
    if (deps[i].source.kind == OAK_PKG_SOURCE_PATH)
    {
      char* joined =
          oak_pkg_path_join(t->a, dir, deps[i].source.location, OAK_HERE);
      child_dir = joined ? oak_pkg_path_abs(t->a, joined, OAK_HERE) : OAK_NULL;
      oak_free(t->a, joined, OAK_HERE);
    }
    else
    {
      const oak_pkg_lock_entry_t* e =
          oak_pkg_lock_find(&t->lock, deps[i].source.location);
      if (e)
        child_dir = oak_pkg_cache_dir(t->a, t->cache_root, &deps[i].source,
                                      e->rev, e->sha256, OAK_HERE);
    }

    oak_pkg_manifest_t child;
    int have_child = 0;
    if (child_dir)
    {
      char reason[OAK_PKG_ERROR_MAX];
      have_child = oak_pkg_manifest_read_dir(&child, t->a, child_dir,
                                             deps[i].alias, reason,
                                             sizeof reason) == 0;
    }

    if (have_child)
    {
      char version[64];
      oak_semver_format(&child.version, version, sizeof version);
      printf("%s (%s %s)", deps[i].alias, child.name, version);
      if (child.has_native)
        fputs(" [native]", stdout);
      if (deps[i].source.kind == OAK_PKG_SOURCE_PATH)
        printf(" -> %s", deps[i].source.location);
      fputc('\n', stdout);
      print_tree(t, &child, child_dir, depth + 1);
      oak_pkg_manifest_free(&child);
    }
    else
    {
      printf("%s (not installed -- run 'oak-pkg install')\n", deps[i].alias);
    }
    oak_free(t->a, child_dir, OAK_HERE);
  }
}

int oak_pkg_cmd_tree(oak_pkg_tool_t* t, const int argc, const char* const* argv)
{
  (void)argc;
  (void)argv;
  oak_pkg_manifest_t root;
  if (read_root(t, &root) != 0)
    return -1;

  char version[64];
  oak_semver_format(&root.version, version, sizeof version);
  printf("%s %s\n", root.name, version);
  print_tree(t, &root, t->project_dir, 1);

  oak_pkg_manifest_free(&root);
  return 0;
}

/* Verify one locked package: present, and saying about itself what the lock
 * says about it. */
static int check_entry(oak_pkg_tool_t* t,
                       const oak_pkg_lock_entry_t* e,
                       int* problems)
{
  oak_pkg_source_t source;
  memset(&source, 0, sizeof source);
  source.kind = e->kind;
  source.location = e->location;
  source.strip = e->strip;

  char* dir = oak_pkg_cache_dir(t->a, t->cache_root, &source, e->rev, e->sha256,
                                OAK_HERE);
  if (!dir)
    return -1;

  char locked[64];
  oak_semver_format(&e->version, locked, sizeof locked);

  if (!oak_pkg_is_dir(dir))
  {
    printf("  %-28s missing from the cache\n", e->name);
    ++*problems;
    oak_free(t->a, dir, OAK_HERE);
    return 0;
  }

  oak_pkg_manifest_t m;
  char reason[OAK_PKG_ERROR_MAX] = { 0 };
  const int read = oak_pkg_manifest_read_dir(&m, t->a, dir, e->name, reason,
                                             sizeof reason) == 0;
  if (!read)
  {
    printf("  %-28s %s\n", e->name, reason);
    ++*problems;
    oak_free(t->a, dir, OAK_HERE);
    return 0;
  }

  char actual[64];
  oak_semver_format(&m.version, actual, sizeof actual);
  if (strcmp(m.name, e->name) != 0 || strcmp(actual, locked) != 0)
  {
    printf("  %-28s cache holds %s %s\n", e->name, m.name, actual);
    ++*problems;
  }
  else if (m.has_native)
  {
    /* The check an author most wants before publishing: that the binary for
     * this platform is actually there and actually loadable. */
    char* lib = oak_pkg_native_lib_path(t->a, dir, &m.native, OAK_HERE);
    if (!lib || !oak_pkg_is_dir(dir))
      ++*problems;
    else if (m.native.abi != (int)OAK_PLUGIN_ABI)
    {
      printf("  %-28s plugin ABI %d, this oak speaks %u\n", e->name,
             m.native.abi, (unsigned)OAK_PLUGIN_ABI);
      ++*problems;
    }
    else
    {
      FILE* probe = fopen(lib, "rb");
      if (probe)
      {
        fclose(probe);
        printf("  %-28s %s [native, %s]\n", e->name, locked, OAK_PLATFORM);
      }
      else
      {
        printf("  %-28s no native library for %s\n", e->name, OAK_PLATFORM);
        ++*problems;
      }
    }
    oak_free(t->a, lib, OAK_HERE);
  }
  else
  {
    printf("  %-28s %s\n", e->name, locked);
  }

  oak_pkg_manifest_free(&m);
  oak_free(t->a, dir, OAK_HERE);
  return 0;
}

int oak_pkg_cmd_check(oak_pkg_tool_t* t,
                      const int argc,
                      const char* const* argv)
{
  (void)argc;
  (void)argv;
  oak_pkg_manifest_t root;
  if (read_root(t, &root) != 0)
    return -1;

  int problems = 0;

  /* Every fetched dependency the manifest names has to be in the lock, or the
   * lock is stale and running the program would fail with a worse message. */
  const oak_pkg_dep_t* deps = OAK_CDATA(oak_pkg_dep_t, root.deps);
  for (usize i = 0; i < oak_size(root.deps); ++i)
  {
    if (deps[i].source.kind == OAK_PKG_SOURCE_PATH)
      continue;
    if (!oak_pkg_lock_find(&t->lock, deps[i].source.location))
    {
      printf("  %-28s not in oak.lock\n", deps[i].alias);
      ++problems;
    }
  }

  const usize count = t->have_lock ? oak_size(t->lock.entries) : 0u;
  const oak_pkg_lock_entry_t* entries =
      count ? OAK_CDATA(oak_pkg_lock_entry_t, t->lock.entries) : OAK_NULL;
  for (usize i = 0; i < count; ++i)
    check_entry(t, &entries[i], &problems);

  oak_pkg_manifest_free(&root);

  if (problems == 0)
  {
    printf("%zu package%s, all present\n", count, count == 1u ? "" : "s");
    return 0;
  }
  fprintf(stderr, "\n%d problem%s; 'oak-pkg install' fixes most of them\n",
          problems, problems == 1 ? "" : "s");
  return -1;
}

int oak_pkg_cmd_cache(oak_pkg_tool_t* t,
                      const int argc,
                      const char* const* argv)
{
  const char* what = positional(argc, argv);
  if (!t->cache_root)
    return complain("cannot locate the package cache; set OAK_PACKAGE_CACHE");

  if (!what || strcmp(what, "path") == 0)
  {
    /* stdout, unadorned, so it can be assigned to a shell variable. */
    printf("%s\n", t->cache_root);
    return 0;
  }

  if (strcmp(what, "clean") == 0)
  {
    if (!oak_pkg_is_dir(t->cache_root))
    {
      oak_pkg_say(t, "the cache is already empty");
      return 0;
    }
    if (oak_pkg_rmtree(t->a, t->cache_root) != 0)
      return complain("cannot remove '%s'", t->cache_root);
    oak_pkg_say(t, "removed %s", t->cache_root);
    return 0;
  }

  return complain("'cache %s' is not a thing; try 'path' or 'clean'", what);
}
