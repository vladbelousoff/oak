#include "oak_pkg_lock.h"

#include "internal/oak_pkg_util.h"

#include "oak_log.h"

#include "yyjson.h"

#include <stdio.h>

static const char* str_or(yyjson_val* obj, const char* key, const char* fallback)
{
  yyjson_val* v = yyjson_obj_get(obj, key);
  return (v && yyjson_is_str(v)) ? yyjson_get_str(v) : fallback;
}

static void entry_free(oak_allocator_t* a, oak_pkg_lock_entry_t* e)
{
  oak_free(a, e->name, OAK_HERE);
  oak_free(a, e->module, OAK_HERE);
  oak_free(a, e->src, OAK_HERE);
  oak_free(a, e->location, OAK_HERE);
  oak_free(a, e->rev, OAK_HERE);
  oak_free(a, e->sha256, OAK_HERE);
  memset(e, 0, sizeof *e);
}

static int parse_entry(oak_pkg_lock_t* lock,
                       yyjson_val* item,
                       const usize index,
                       char* err,
                       const usize err_cap)
{
  if (!yyjson_is_obj(item))
    return oak_pkg_fail(err, err_cap, "package %zu is not an object", index);

  const char* name = str_or(item, "name", OAK_NULL);
  const char* version = str_or(item, "version", OAK_NULL);
  if (!name || !version)
    return oak_pkg_fail(err, err_cap,
                        "package %zu is missing 'name' or 'version'", index);

  oak_pkg_lock_entry_t e;
  memset(&e, 0, sizeof e);
  e.strip = 1;

  if (oak_semver_parse(version, &e.version) != 0)
    return oak_pkg_fail(err, err_cap, "package '%s': '%s' is not a version",
                        name, version);

  yyjson_val* source = yyjson_obj_get(item, "source");
  if (!yyjson_is_obj(source))
    return oak_pkg_fail(err, err_cap, "package '%s' has no 'source'", name);

  const char* git = str_or(source, "git", OAK_NULL);
  const char* url = str_or(source, "url", OAK_NULL);
  const char* rev = str_or(source, "rev", OAK_NULL);
  const char* sha256 = str_or(source, "sha256", OAK_NULL);

  /* The lock exists to remove every remaining question, so an entry that still
   * leaves one -- which commit, which bytes -- is rejected rather than fetched
   * on trust. A stale lock should fail loudly and be regenerated. */
  if (git)
  {
    if (!rev || !rev[0])
      return oak_pkg_fail(err, err_cap,
                          "package '%s' locks a git source with no 'rev'; "
                          "run oak-pkg install to regenerate oak.lock",
                          name);
    e.kind = OAK_PKG_SOURCE_GIT;
    e.location = oak_pkg_strdup(lock->allocator, git, OAK_HERE);
    e.rev = oak_pkg_strdup(lock->allocator, rev, OAK_HERE);
  }
  else if (url)
  {
    if (!sha256 || !sha256[0])
      return oak_pkg_fail(err, err_cap,
                          "package '%s' locks an archive with no 'sha256'; "
                          "run oak-pkg install to regenerate oak.lock",
                          name);
    e.kind = OAK_PKG_SOURCE_URL;
    e.location = oak_pkg_strdup(lock->allocator, url, OAK_HERE);
    e.sha256 = oak_pkg_strdup(lock->allocator, sha256, OAK_HERE);
    yyjson_val* strip = yyjson_obj_get(source, "strip");
    if (strip && yyjson_is_int(strip))
      e.strip = (int)yyjson_get_int(strip);
  }
  else
  {
    return oak_pkg_fail(err, err_cap,
                        "package '%s' has a source that is neither 'git' nor "
                        "'url'",
                        name);
  }

  e.name = oak_pkg_strdup(lock->allocator, name, OAK_HERE);
  e.module = oak_pkg_strdup(lock->allocator, str_or(item, "module", name),
                            OAK_HERE);
  e.src = oak_pkg_strdup(lock->allocator, str_or(item, "src", "."), OAK_HERE);

  if (!e.name || !e.module || !e.src || !e.location)
  {
    entry_free(lock->allocator, &e);
    return -1;
  }

  OAK_ASSERT(oak_push_back(lock->entries, &e));
  return 0;
}

int oak_pkg_lock_read(oak_pkg_lock_t* out,
                      oak_allocator_t* a,
                      const char* path,
                      char* err,
                      const usize err_cap)
{
  if (!out || !a || !path)
    return -1;
  memset(out, 0, sizeof *out);
  out->allocator = a;
  out->entries = oak_vector_new(a, sizeof(oak_pkg_lock_entry_t));
  if (!out->entries)
    return -1;

  /* No lock at all is the normal state of a project with only path
   * dependencies, so it is an empty answer rather than a failure. */
  FILE* probe = fopen(path, "rb");
  if (!probe)
    return 0;
  fclose(probe);

  usize len = 0;
  char* text = oak_pkg_read_file(a, path, &len, err, err_cap, OAK_HERE);
  if (!text)
  {
    oak_pkg_lock_free(out);
    return -1;
  }

  yyjson_read_err rerr;
  memset(&rerr, 0, sizeof rerr);
  yyjson_doc* doc = yyjson_read_opts(text, len, 0, OAK_NULL, &rerr);
  oak_free(a, text, OAK_HERE);
  if (!doc)
  {
    oak_pkg_fail(err, err_cap, "%s: invalid JSON at byte %zu: %s", path,
                 (usize)rerr.pos, rerr.msg ? rerr.msg : "parse error");
    oak_pkg_lock_free(out);
    return -1;
  }

  int rc = -1;
  do
  {
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root))
    {
      oak_pkg_fail(err, err_cap, "%s: the lockfile must be a JSON object",
                   path);
      break;
    }

    yyjson_val* lock_version = yyjson_obj_get(root, "lock");
    const int version =
        (lock_version && yyjson_is_int(lock_version))
            ? (int)yyjson_get_int(lock_version)
            : 0;
    /* A newer lock may pin things this build cannot honour, and guessing is
     * worse than saying so. */
    if (version != OAK_PKG_LOCK_VERSION)
    {
      oak_pkg_fail(err, err_cap,
                   "%s: lock format %d, but this oak understands %d; "
                   "regenerate it with a matching oak-pkg",
                   path, version, OAK_PKG_LOCK_VERSION);
      break;
    }

    yyjson_val* packages = yyjson_obj_get(root, "packages");
    if (packages && !yyjson_is_null(packages))
    {
      if (!yyjson_is_arr(packages))
      {
        oak_pkg_fail(err, err_cap, "%s: 'packages' must be an array", path);
        break;
      }
      usize index = 0;
      usize max = 0;
      yyjson_val* item;
      yyjson_arr_iter iter;
      yyjson_arr_iter_init(packages, &iter);
      int failed = 0;
      while ((item = yyjson_arr_iter_next(&iter)) != OAK_NULL)
      {
        if (parse_entry(out, item, index, err, err_cap) != 0)
        {
          failed = 1;
          break;
        }
        ++index;
        ++max;
      }
      (void)max;
      if (failed)
      {
        /* Prefix the file so the reader knows which lock to regenerate. */
        if (err && err_cap > 0u)
        {
          char detail[OAK_PKG_ERROR_MAX];
          snprintf(detail, sizeof detail, "%s", err);
          snprintf(err, err_cap, "%s: %s", path, detail);
        }
        break;
      }
    }

    rc = 0;
  } while (0);

  yyjson_doc_free(doc);
  if (rc != 0)
    oak_pkg_lock_free(out);
  return rc;
}

void oak_pkg_lock_free(oak_pkg_lock_t* lock)
{
  if (!lock || !lock->allocator)
    return;
  if (lock->entries)
  {
    oak_pkg_lock_entry_t* entries =
        OAK_DATA(oak_pkg_lock_entry_t, lock->entries);
    for (usize i = 0; i < oak_size(lock->entries); ++i)
      entry_free(lock->allocator, &entries[i]);
    oak_destroy(lock->entries);
  }
  memset(lock, 0, sizeof *lock);
}

const oak_pkg_lock_entry_t* oak_pkg_lock_find(const oak_pkg_lock_t* lock,
                                              const char* location)
{
  if (!lock || !lock->entries || !location)
    return OAK_NULL;
  const oak_pkg_lock_entry_t* entries =
      OAK_CDATA(oak_pkg_lock_entry_t, lock->entries);
  for (usize i = 0; i < oak_size(lock->entries); ++i)
    if (strcmp(entries[i].location, location) == 0)
      return &entries[i];
  return OAK_NULL;
}
