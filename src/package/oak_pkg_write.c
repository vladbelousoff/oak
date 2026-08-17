/*
 * Producing the two files oak-pkg owns: oak.lock, and edits to oak.json.
 *
 * The lockfile is generated wholesale, so it is written from scratch every
 * time.  The manifest is not: it is a file a person wrote, so `add` and
 * `remove` reload it, change the one key they are responsible for, and write
 * the document back.  That preserves field order, formatting choices, and --
 * the part that matters -- keys this build has never heard of, so a project
 * shared with a newer oak-pkg does not quietly lose configuration when someone
 * on an older one runs `add`.
 */

#include "internal/oak_pkg_util.h"

#include "oak_log.h"
#include "oak_pkg_fs.h"
#include "oak_pkg_lock.h"
#include "oak_pkg_manifest.h"

#include "yyjson.h"

#include <stdio.h>

#define WRITE_FLAGS                                                            \
  (YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END)

/* Write `doc` to `path` via a temporary file, so an interrupted write leaves
 * the previous manifest intact rather than a truncated one. */
static int write_doc(oak_allocator_t* a,
                     yyjson_mut_doc* doc,
                     const char* path,
                     char* err,
                     const usize err_cap)
{
  usize len = 0;
  yyjson_write_err werr;
  memset(&werr, 0, sizeof werr);
  char* text = yyjson_mut_write_opts(doc, WRITE_FLAGS, OAK_NULL, &len, &werr);
  if (!text)
    return oak_pkg_fail(err, err_cap, "cannot serialize '%s': %s", path,
                        werr.msg ? werr.msg : "write error");

  const usize plen = strlen(path);
  char* tmp = oak_alloc(a, plen + 5u, OAK_HERE);
  if (!tmp)
  {
    free(text);
    return -1;
  }
  snprintf(tmp, plen + 5u, "%s.tmp", path);

  int rc = 0;
  if (oak_pkg_write_file(tmp, text, len) != 0)
    rc = oak_pkg_fail(err, err_cap, "cannot write '%s'", tmp);
  else if (remove(path) != 0 && oak_pkg_is_dir(path))
    rc = oak_pkg_fail(err, err_cap, "'%s' is a directory", path);
  else if (rename(tmp, path) != 0)
    rc = oak_pkg_fail(err, err_cap, "cannot replace '%s'", path);

  if (rc != 0)
    remove(tmp);
  oak_free(a, tmp, OAK_HERE);
  free(text);
  return rc;
}

int oak_pkg_lock_write(const oak_pkg_lock_t* lock,
                       const char* path,
                       char* err,
                       const usize err_cap)
{
  if (!lock || !lock->allocator || !path)
    return -1;

  yyjson_mut_doc* doc = yyjson_mut_doc_new(OAK_NULL);
  if (!doc)
    return -1;
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  /* First key, so a human opening the file sees what format it is before
   * anything else. */
  yyjson_mut_obj_add_int(doc, root, "lock", OAK_PKG_LOCK_VERSION);
  yyjson_mut_val* packages = yyjson_mut_arr(doc);
  yyjson_mut_obj_add_val(doc, root, "packages", packages);

  const oak_pkg_lock_entry_t* e = OAK_CDATA(oak_pkg_lock_entry_t, lock->entries);
  for (usize i = 0; i < oak_size(lock->entries); ++i)
  {
    char version[64];
    if (oak_semver_format(&e[i].version, version, sizeof version) < 0)
    {
      yyjson_mut_doc_free(doc);
      return oak_pkg_fail(err, err_cap, "package '%s' has no usable version",
                          e[i].name);
    }

    yyjson_mut_val* item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, item, "name", e[i].name);
    yyjson_mut_obj_add_strcpy(doc, item, "version", version);
    yyjson_mut_obj_add_strcpy(doc, item, "module", e[i].module);
    yyjson_mut_obj_add_strcpy(doc, item, "src", e[i].src);

    yyjson_mut_val* source = yyjson_mut_obj(doc);
    if (e[i].kind == OAK_PKG_SOURCE_GIT)
    {
      yyjson_mut_obj_add_strcpy(doc, source, "git", e[i].location);
      yyjson_mut_obj_add_strcpy(doc, source, "rev", e[i].rev);
    }
    else
    {
      yyjson_mut_obj_add_strcpy(doc, source, "url", e[i].location);
      yyjson_mut_obj_add_strcpy(doc, source, "sha256", e[i].sha256);
      yyjson_mut_obj_add_int(doc, source, "strip", e[i].strip);
    }
    yyjson_mut_obj_add_val(doc, item, "source", source);
    yyjson_mut_arr_append(packages, item);
  }

  const int rc = write_doc(lock->allocator, doc, path, err, err_cap);
  yyjson_mut_doc_free(doc);
  return rc;
}

/* Load `path` as a mutable document. */
static yyjson_mut_doc* load_mut(oak_allocator_t* a,
                                const char* path,
                                char* err,
                                const usize err_cap)
{
  usize len = 0;
  char* text = oak_pkg_read_file(a, path, &len, err, err_cap, OAK_HERE);
  if (!text)
    return OAK_NULL;

  yyjson_read_err rerr;
  memset(&rerr, 0, sizeof rerr);
  yyjson_doc* doc = yyjson_read_opts(text, len, 0, OAK_NULL, &rerr);
  oak_free(a, text, OAK_HERE);
  if (!doc)
  {
    oak_pkg_fail(err, err_cap, "%s: invalid JSON at byte %zu: %s", path,
                 (usize)rerr.pos, rerr.msg ? rerr.msg : "parse error");
    return OAK_NULL;
  }

  yyjson_mut_doc* mut = yyjson_doc_mut_copy(doc, OAK_NULL);
  yyjson_doc_free(doc);
  if (!mut)
    return OAK_NULL;

  if (!yyjson_mut_is_obj(yyjson_mut_doc_get_root(mut)))
  {
    yyjson_mut_doc_free(mut);
    oak_pkg_fail(err, err_cap, "%s: the manifest must be a JSON object", path);
    return OAK_NULL;
  }
  return mut;
}

int oak_pkg_manifest_init_file(oak_allocator_t* a,
                               const char* path,
                               const char* name,
                               const char* module,
                               const char* src,
                               char* err,
                               const usize err_cap)
{
  if (!a || !path || !name)
    return -1;

  yyjson_mut_doc* doc = yyjson_mut_doc_new(OAK_NULL);
  if (!doc)
    return -1;
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  yyjson_mut_obj_add_strcpy(doc, root, "name", name);
  yyjson_mut_obj_add_strcpy(doc, root, "version", "0.1.0");
  if (module)
    yyjson_mut_obj_add_strcpy(doc, root, "module", module);
  yyjson_mut_obj_add_strcpy(doc, root, "src", src ? src : "src");
  /* An empty deps object rather than none, so the first `oak-pkg add` edits a
   * key that is already there and a reader can see where dependencies go. */
  yyjson_mut_obj_add_val(doc, root, "deps", yyjson_mut_obj(doc));

  const int rc = write_doc(a, doc, path, err, err_cap);
  yyjson_mut_doc_free(doc);
  return rc;
}

int oak_pkg_manifest_add_dep(oak_allocator_t* a,
                             const char* path,
                             const char* alias,
                             const oak_pkg_source_t* source,
                             const oak_semver_req_t* req,
                             char* err,
                             const usize err_cap)
{
  if (!a || !path || !alias || !source)
    return -1;

  yyjson_mut_doc* doc = load_mut(a, path, err, err_cap);
  if (!doc)
    return -1;
  yyjson_mut_val* root = yyjson_mut_doc_get_root(doc);

  yyjson_mut_val* deps = yyjson_mut_obj_get(root, "deps");
  if (!deps || !yyjson_mut_is_obj(deps))
  {
    deps = yyjson_mut_obj(doc);
    yyjson_mut_obj_put(root, yyjson_mut_strcpy(doc, "deps"), deps);
  }

  /* Always the long form. The shorthand is nicer to type than to generate:
   * writing it back would have to re-derive a spec from a URL and a tag, and
   * would silently drop a digest that has no shorthand spelling. */
  yyjson_mut_val* entry = yyjson_mut_obj(doc);
  switch (source->kind)
  {
    case OAK_PKG_SOURCE_PATH:
      yyjson_mut_obj_add_strcpy(doc, entry, "path", source->location);
      break;
    case OAK_PKG_SOURCE_GIT:
      yyjson_mut_obj_add_strcpy(doc, entry, "git", source->location);
      if (source->tag)
        yyjson_mut_obj_add_strcpy(doc, entry, "tag", source->tag);
      if (source->rev)
        yyjson_mut_obj_add_strcpy(doc, entry, "rev", source->rev);
      break;
    case OAK_PKG_SOURCE_URL:
      yyjson_mut_obj_add_strcpy(doc, entry, "url", source->location);
      if (source->sha256)
        yyjson_mut_obj_add_strcpy(doc, entry, "sha256", source->sha256);
      if (source->strip != 1)
        yyjson_mut_obj_add_int(doc, entry, "strip", source->strip);
      break;
  }

  if (req && req->op != OAK_SEMVER_ANY)
  {
    char text[80];
    char version[64];
    if (oak_semver_format(&req->version, version, sizeof version) >= 0)
    {
      const char* prefix = req->op == OAK_SEMVER_CARET
                               ? "^"
                               : (req->op == OAK_SEMVER_ATLEAST ? ">=" : "");
      snprintf(text, sizeof text, "%s%s", prefix, version);
      yyjson_mut_obj_add_strcpy(doc, entry, "version", text);
    }
  }

  /* put, not add: re-adding an alias replaces it rather than leaving two
   * entries for one import namespace. */
  yyjson_mut_obj_put(deps, yyjson_mut_strcpy(doc, alias), entry);

  const int rc = write_doc(a, doc, path, err, err_cap);
  yyjson_mut_doc_free(doc);
  return rc;
}

int oak_pkg_manifest_remove_dep(oak_allocator_t* a,
                                const char* path,
                                const char* alias,
                                char* err,
                                const usize err_cap)
{
  if (!a || !path || !alias)
    return -1;

  yyjson_mut_doc* doc = load_mut(a, path, err, err_cap);
  if (!doc)
    return -1;
  yyjson_mut_val* root = yyjson_mut_doc_get_root(doc);

  yyjson_mut_val* deps = yyjson_mut_obj_get(root, "deps");
  if (!deps || !yyjson_mut_is_obj(deps) ||
      !yyjson_mut_obj_remove_key(deps, alias))
  {
    yyjson_mut_doc_free(doc);
    return oak_pkg_fail(err, err_cap, "there is no dependency called '%s'",
                        alias);
  }

  const int rc = write_doc(a, doc, path, err, err_cap);
  yyjson_mut_doc_free(doc);
  return rc;
}
