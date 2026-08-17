#include "oak_package.h"

#include "internal/oak_module_loader.h"

#include "internal/oak_pkg_util.h"
#include "oak_module_loader.h"
#include "oak_module_mount.h"
#include "oak_pkg_cache.h"
#include "oak_pkg_fs.h"
#include "oak_pkg_lock.h"
#include "oak_pkg_manifest.h"
#include "oak_plugin.h"
#include "oak_plugin_host.h"
#include "oak_version.h"

/* Name of a project or package manifest, and of the resolved graph beside it. */
#define MANIFEST_NAME "oak.json"
#define LOCK_NAME "oak.lock"

/* How far up the tree to look for a manifest. Deep enough for any real layout,
 * shallow enough that a script outside a project does not stat its way to the
 * filesystem root on every run. */
#define MANIFEST_SEARCH_DEPTH 32

/* One package: where it is, where its modules are, and what it declares. */
typedef struct pkg_node pkg_node_t;
struct pkg_node
{
  /* Canonical directory containing the manifest. Owned. */
  char* dir;
  /* Canonical directory dotted names resolve against. Owned. */
  char* src_dir;
  oak_pkg_manifest_t manifest;
};

struct oak_package_set
{
  oak_allocator_t* allocator;
  /* Canonical project directory, or null when no manifest was found. Owned. */
  char* root_dir;
  oak_pkg_lock_t lock;
  /* Vector of pkg_node_t; index 0 is the project itself. */
  oak_container_t* nodes;
  /* Vector of oak_plugin_lib_t, unloaded last of everything. */
  oak_container_t* plugins;
  /* Cleared by oak_package_set_allow_plugins, for running code you have not
   * read yet: a native package is arbitrary machine code, and a source-only
   * dependency graph is a meaningfully smaller thing to trust. */
  int allow_plugins;
};


static void set_error(oak_diagnostic_t* err, const char* message)
{
  if (!err)
    return;
  err->line = 0;
  err->column = 0;
  snprintf(err->message, sizeof err->message, "%s", message);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static int fail(oak_diagnostic_t* err, const char* fmt, ...)
{
  if (err)
  {
    err->line = 0;
    err->column = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof err->message, fmt, ap);
    va_end(ap);
  }
  return -1;
}

/* The node whose directory is `dir`, or null. Package identity is the resolved
 * directory rather than the name, so the same checkout reached through two
 * different aliases is loaded once. */
static pkg_node_t* find_node(const oak_package_set_t* set, const char* dir)
{
  pkg_node_t* nodes = OAK_DATA(pkg_node_t, set->nodes);
  for (usize i = 0; i < oak_size(set->nodes); ++i)
    if (strcmp(nodes[i].dir, dir) == 0)
      return &nodes[i];
  return OAK_NULL;
}

/* Read the manifest in `dir` and append it as a node. Returns the node, or null
 * with `err` set. `dir` is consumed on success and freed on failure. */
static pkg_node_t* add_node(oak_package_set_t* set,
                            char* dir,
                            oak_diagnostic_t* err)
{
  oak_allocator_t* a = set->allocator;

  char* manifest_path = path_join(a, dir, MANIFEST_NAME);
  if (!manifest_path)
  {
    oak_free(a, dir, OAK_HERE);
    return OAK_NULL;
  }

  pkg_node_t node;
  memset(&node, 0, sizeof node);

  char reason[OAK_PKG_ERROR_MAX] = { 0 };
  const int rc = oak_pkg_manifest_read(&node.manifest, a, manifest_path, reason,
                                       sizeof reason);
  oak_free(a, manifest_path, OAK_HERE);
  if (rc != 0)
  {
    set_error(err, reason);
    oak_free(a, dir, OAK_HERE);
    return OAK_NULL;
  }

  /* A package that needs a newer runtime says so here rather than failing
   * later as a syntax error on a construct this build has never heard of. */
  oak_semver_t running;
  if (oak_semver_parse(OAK_VERSION_STRING, &running) == 0 &&
      !oak_semver_req_match(&node.manifest.oak_req, &running))
  {
    fail(err, "package '%s' needs a different oak; this is %s",
         node.manifest.name, OAK_VERSION_STRING);
    oak_pkg_manifest_free(&node.manifest);
    oak_free(a, dir, OAK_HERE);
    return OAK_NULL;
  }

  char* src_raw = path_join(a, dir, node.manifest.src);
  node.src_dir = src_raw ? path_canonicalize(a, src_raw) : OAK_NULL;
  oak_free(a, src_raw, OAK_HERE);
  if (!node.src_dir || !path_dir_exists(node.src_dir))
  {
    fail(err, "package '%s': source directory '%s' does not exist",
         node.manifest.name, node.manifest.src);
    oak_free(a, node.src_dir, OAK_HERE);
    oak_pkg_manifest_free(&node.manifest);
    oak_free(a, dir, OAK_HERE);
    return OAK_NULL;
  }

  node.dir = dir;
  OAK_ASSERT(oak_push_back(set->nodes, &node));
  return OAK_DATA(pkg_node_t, set->nodes) + (oak_size(set->nodes) - 1u);
}

/* Where a dependency's files are, as an owned canonical directory. Path
 * dependencies resolve against the manifest that names them; fetched ones come
 * from the lock, because the lock is the only place their exact tree is
 * recorded. */
static char* dep_dir(oak_package_set_t* set,
                     const pkg_node_t* from,
                     const oak_pkg_dep_t* dep,
                     oak_diagnostic_t* err)
{
  oak_allocator_t* a = set->allocator;

  if (dep->source.kind == OAK_PKG_SOURCE_PATH)
  {
    char* joined = path_join(a, from->dir, dep->source.location);
    if (!joined)
      return OAK_NULL;
    char* resolved = path_canonicalize(a, joined);
    oak_free(a, joined, OAK_HERE);
    if (!resolved)
      return OAK_NULL;
    if (!path_dir_exists(resolved))
    {
      fail(err, "package '%s': dependency '%s' points at '%s', which is not a "
                "directory",
           from->manifest.name, dep->alias, dep->source.location);
      oak_free(a, resolved, OAK_HERE);
      return OAK_NULL;
    }
    return resolved;
  }

  const oak_pkg_lock_entry_t* locked =
      oak_pkg_lock_find(&set->lock, dep->source.location);
  if (!locked)
  {
    fail(err,
         "package '%s': dependency '%s' is not in oak.lock; run 'oak-pkg "
         "install'",
         from->manifest.name, dep->alias);
    return OAK_NULL;
  }

  char* cache_root = oak_pkg_cache_root(a, OAK_HERE);
  if (!cache_root)
  {
    fail(err, "cannot locate the package cache; set OAK_PACKAGE_CACHE");
    return OAK_NULL;
  }
  char* dir = oak_pkg_cache_dir(a, cache_root, &dep->source, locked->rev,
                               locked->sha256, OAK_HERE);
  oak_free(a, cache_root, OAK_HERE);
  if (!dir)
    return OAK_NULL;

  /* Present in the lock but absent from the cache is the ordinary state of a
   * fresh clone, so the message says what to run rather than what is missing. */
  if (!path_dir_exists(dir))
  {
    fail(err,
         "package '%s': dependency '%s' is locked but not downloaded; run "
         "'oak-pkg install'",
         from->manifest.name, dep->alias);
    oak_free(a, dir, OAK_HERE);
    return OAK_NULL;
  }
  return dir;
}

/* Load every package reachable from node `index`, breadth first. Nodes are
 * appended as they are discovered, and the loop walks the growing vector, so a
 * package reached twice is loaded once and a cycle terminates. */
static int load_graph(oak_package_set_t* set, oak_diagnostic_t* err)
{
  for (usize i = 0; i < oak_size(set->nodes); ++i)
  {
    /* Re-read the node each iteration: adding one may reallocate the vector,
     * so a pointer taken before the inner loop would dangle. */
    for (usize d = 0;; ++d)
    {
      pkg_node_t* node = OAK_DATA(pkg_node_t, set->nodes) + i;
      if (d >= oak_size(node->manifest.deps))
        break;

      const oak_pkg_dep_t* dep =
          OAK_CDATA(oak_pkg_dep_t, node->manifest.deps) + d;

      char* dir = dep_dir(set, node, dep, err);
      if (!dir)
        return -1;

      if (find_node(set, dir))
      {
        oak_free(set->allocator, dir, OAK_HERE);
        continue;
      }
      if (!add_node(set, dir, err))
        return -1;
    }
  }
  return 0;
}

/* First directory at or above `start` holding a manifest, or null. */
static char* find_project_root(oak_allocator_t* a, const char* start)
{
  char* dir = path_canonicalize(a, start);
  if (!dir)
    return OAK_NULL;

  for (int depth = 0; depth < MANIFEST_SEARCH_DEPTH; ++depth)
  {
    char* candidate = path_join(a, dir, MANIFEST_NAME);
    if (!candidate)
      break;
    const int found = path_exists(candidate);
    oak_free(a, candidate, OAK_HERE);
    if (found)
      return dir;

    char* parent = path_dirname_dup(a, dir);
    if (!parent)
      break;
    /* dirname of a root is the root, which is where the walk ends. */
    if (strcmp(parent, dir) == 0)
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

oak_package_set_t* oak_package_set_open(const char* entry_path,
                                        oak_allocator_t* a,
                                        oak_diagnostic_t* err)
{
  if (!entry_path || !a)
    return OAK_NULL;

  oak_package_set_t* set = oak_alloc(a, sizeof *set, OAK_HERE);
  if (!set)
    return OAK_NULL;
  memset(set, 0, sizeof *set);
  set->allocator = a;
  set->allow_plugins = 1;
  set->nodes = oak_vector_new(a, sizeof(pkg_node_t));
  set->plugins = oak_vector_new(a, sizeof(oak_plugin_lib_t));
  if (!set->nodes || !set->plugins)
  {
    oak_package_set_close(set);
    return OAK_NULL;
  }

  char* entry_dir = path_dirname_dup(a, entry_path);
  if (!entry_dir)
  {
    oak_package_set_close(set);
    return OAK_NULL;
  }
  set->root_dir = find_project_root(a, entry_dir);
  oak_free(a, entry_dir, OAK_HERE);

  /* No manifest anywhere above the script: a valid, empty set. Every import
   * then resolves the way it did before packages existed. */
  if (!set->root_dir)
    return set;

  char* lock_path = path_join(a, set->root_dir, LOCK_NAME);
  if (!lock_path)
  {
    oak_package_set_close(set);
    return OAK_NULL;
  }
  char reason[OAK_PKG_ERROR_MAX] = { 0 };
  const int lock_rc =
      oak_pkg_lock_read(&set->lock, a, lock_path, reason, sizeof reason);
  oak_free(a, lock_path, OAK_HERE);
  if (lock_rc != 0)
  {
    set_error(err, reason);
    oak_package_set_close(set);
    return OAK_NULL;
  }

  char* root_copy = oak_pkg_strdup(a, set->root_dir, OAK_HERE);
  if (!root_copy || !add_node(set, root_copy, err) ||
      load_graph(set, err) != 0)
  {
    oak_package_set_close(set);
    return OAK_NULL;
  }

  return set;
}

/* Name the platforms a package does ship, so someone on an unsupported one can
 * see at a glance whether it is a gap or a mistake. Best effort: an unreadable
 * directory just means a shorter message. */
static void describe_platforms(const oak_package_set_t* set,
                               const pkg_node_t* node,
                               char* out,
                               const usize cap)
{
  out[0] = 0;
  char* dir = oak_pkg_path_join(set->allocator, node->dir,
                                node->manifest.native.dir, OAK_HERE);
  if (!dir)
    return;
  oak_container_t* names = oak_pkg_list_dir(set->allocator, dir);
  oak_free(set->allocator, dir, OAK_HERE);
  if (!names)
    return;

  usize w = 0;
  const char** items = OAK_CDATA(char*, names);
  for (usize i = 0; i < oak_size(names) && w + 2u < cap; ++i)
  {
    const int n = snprintf(out + w, cap - w, "%s%s", w ? ", " : "", items[i]);
    if (n < 0 || (usize)n >= cap - w)
      break;
    w += (usize)n;
  }
  oak_pkg_list_free(set->allocator, names);
}

/* Load one package's shared library and let it register its bindings.
 *
 * Runs after every mount is in place, which is the order the whole design
 * turns on: binding makes the namespace a native module, and a mount over a
 * native module is refused. Mount first and the package's own `.oak` stub is
 * what answers the import, with the bindings filling in the bodies. */
static int load_plugin(oak_package_set_t* set,
                       const pkg_node_t* node,
                       oak_compile_options_t* opts,
                       oak_diagnostic_t* err)
{
  oak_allocator_t* a = set->allocator;

  if (node->manifest.native.abi != (int)OAK_PLUGIN_ABI)
    return fail(err,
                "package '%s' declares plugin ABI %d, but this oak speaks %u",
                node->manifest.name, node->manifest.native.abi,
                (unsigned)OAK_PLUGIN_ABI);

  char* lib = oak_pkg_native_lib_path(a, node->dir, &node->manifest.native,
                                      OAK_HERE);
  if (!lib)
    return -1;

  if (!path_exists(lib))
  {
    char shipped[256];
    describe_platforms(set, node, shipped, sizeof shipped);
    if (shipped[0])
      fail(err,
           "package '%s' has no binary for %s; it ships: %s",
           node->manifest.name, OAK_PLATFORM, shipped);
    else
      fail(err, "package '%s' has no binary for %s (looked for %s)",
           node->manifest.name, OAK_PLATFORM, lib);
    oak_free(a, lib, OAK_HERE);
    return -1;
  }

  oak_plugin_lib_t loaded;
  char reason[OAK_PKG_ERROR_MAX] = { 0 };
  const int rc =
      oak_plugin_host_load(lib, node->manifest.name, &loaded, reason,
                           sizeof reason);
  oak_free(a, lib, OAK_HERE);
  if (rc != 0)
  {
    set_error(err, reason);
    return -1;
  }

  /* Recorded before bind runs: if bind registers some of its declarations and
   * then fails, they still point into this library, so it has to stay loaded
   * until the options are torn down. */
  if (!oak_push_back(set->plugins, &loaded))
  {
    oak_plugin_host_unload(&loaded);
    return -1;
  }

  if (loaded.plugin->bind(opts) != 0)
    return fail(err, "package '%s' failed to register its native bindings",
                node->manifest.name);
  return 0;
}

int oak_package_set_apply(oak_package_set_t* set,
                          oak_compile_options_t* opts,
                          oak_diagnostic_t* err)
{
  if (!set || !opts)
    return -1;

  pkg_node_t* nodes = OAK_DATA(pkg_node_t, set->nodes);
  for (usize i = 0; i < oak_size(set->nodes); ++i)
  {
    const pkg_node_t* node = &nodes[i];

    /* A package's own namespace, scoped to itself. Without this a submodule
     * importing a sibling by the package's own name -- `import json.util` from
     * inside json/lexer.oak -- would resolve relative to its own directory and
     * look for json/json/util.oak. */
    if (oak_module_loader_mount(opts, node->dir, node->manifest.module,
                                node->src_dir, node->manifest.name) != 0)
      return fail(err, "package '%s': cannot claim the module namespace '%s'",
                  node->manifest.name, node->manifest.module);

    const oak_pkg_dep_t* deps = OAK_CDATA(oak_pkg_dep_t, node->manifest.deps);
    for (usize d = 0; d < oak_size(node->manifest.deps); ++d)
    {
      char* dir = dep_dir(set, node, &deps[d], err);
      if (!dir)
        return -1;
      const pkg_node_t* target = find_node(set, dir);
      oak_free(set->allocator, dir, OAK_HERE);
      if (!target)
        return fail(err, "package '%s': dependency '%s' was not loaded",
                    node->manifest.name, deps[d].alias);

      /* A native package's bindings are registered once, under the name the
       * library was built with, and the loader matches a stub to them by the
       * module name it was imported as. Renaming one would leave the stub with
       * no implementations and a confusing error about missing bodies, so say
       * so here instead. */
      if (target->manifest.has_native &&
          strcmp(deps[d].alias, target->manifest.module) != 0)
        return fail(err,
                    "package '%s': '%s' ships a native library and must be "
                    "imported as '%s', not renamed to '%s'",
                    node->manifest.name, target->manifest.name,
                    target->manifest.module, deps[d].alias);

      /* The alias, not the target's own module name: renaming a dependency in
       * the manifest is how a program resolves a namespace collision, so the
       * mount has to answer to what the importer writes. */
      if (oak_module_loader_mount(opts, node->dir, deps[d].alias,
                                  target->src_dir,
                                  target->manifest.name) != 0)
        return fail(err, "package '%s': cannot mount dependency '%s'",
                    node->manifest.name, deps[d].alias);
    }
  }

  /* Second pass, deliberately: every mount has to exist before the first
   * binding turns a namespace into a native module. */
  for (usize i = 0; i < oak_size(set->nodes); ++i)
  {
    const pkg_node_t* node = OAK_CDATA(pkg_node_t, set->nodes) + i;
    if (!node->manifest.has_native)
      continue;
    if (!set->allow_plugins)
      return fail(err,
                  "package '%s' needs to load a native library, which "
                  "--no-plugins refuses",
                  node->manifest.name);
    if (load_plugin(set, node, opts, err) != 0)
      return -1;
  }
  return 0;
}

void oak_package_set_allow_plugins(oak_package_set_t* set, const int allow)
{
  if (set)
    set->allow_plugins = allow;
}

const char* oak_package_set_root(const oak_package_set_t* set)
{
  return set ? set->root_dir : OAK_NULL;
}

int oak_package_set_count(const oak_package_set_t* set)
{
  return (set && set->nodes) ? (int)oak_size(set->nodes) : 0;
}

void oak_package_set_close(oak_package_set_t* set)
{
  if (!set)
    return;
  oak_allocator_t* a = set->allocator;

  if (set->nodes)
  {
    pkg_node_t* nodes = OAK_DATA(pkg_node_t, set->nodes);
    for (usize i = 0; i < oak_size(set->nodes); ++i)
    {
      oak_free(a, nodes[i].dir, OAK_HERE);
      oak_free(a, nodes[i].src_dir, OAK_HERE);
      oak_pkg_manifest_free(&nodes[i].manifest);
    }
    oak_destroy(set->nodes);
  }
  oak_pkg_lock_free(&set->lock);
  oak_free(a, set->root_dir, OAK_HERE);

  /* Last of everything this set owns. Bound descriptors were freed with the
   * compile options, but their names and function pointers live in these
   * libraries, so unloading earlier would leave the teardown reading unmapped
   * memory. */
  if (set->plugins)
  {
    oak_plugin_lib_t* libs = OAK_DATA(oak_plugin_lib_t, set->plugins);
    for (usize i = 0; i < oak_size(set->plugins); ++i)
      oak_plugin_host_unload(&libs[i]);
    oak_destroy(set->plugins);
  }

  oak_free(a, set, OAK_HERE);
}
