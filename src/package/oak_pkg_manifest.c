#include "oak_pkg_manifest.h"

#include "internal/oak_pkg_util.h"

#include "oak_log.h"

#include "yyjson.h"

#include <stdlib.h>

/* Borrowed string field, or null when absent. Rejects a present-but-wrong-type
 * field rather than treating it as absent: "version": 1 is a mistake worth
 * naming, not a missing version. */
static int obj_str(yyjson_val* obj,
                   const char* key,
                   const char** out,
                   char* err,
                   const usize err_cap,
                   const char* what)
{
  *out = OAK_NULL;
  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v || yyjson_is_null(v))
    return 0;
  if (!yyjson_is_str(v))
    return oak_pkg_fail(err, err_cap, "%s: '%s' must be a string", what, key);
  *out = yyjson_get_str(v);
  return 0;
}

static int obj_int(yyjson_val* obj,
                   const char* key,
                   int* out,
                   int fallback,
                   char* err,
                   const usize err_cap,
                   const char* what)
{
  *out = fallback;
  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v || yyjson_is_null(v))
    return 0;
  if (!yyjson_is_int(v))
    return oak_pkg_fail(err, err_cap, "%s: '%s' must be an integer", what, key);
  *out = (int)yyjson_get_int(v);
  return 0;
}

/* The last path segment of "acme/json". Package names are namespaced by owner,
 * and the owner is not part of the import spelling. */
static const char* name_tail(const char* name)
{
  const char* slash = strrchr(name, '/');
  return slash ? slash + 1 : name;
}

static int is_hex64(const char* s)
{
  usize n = 0;
  for (; s[n]; ++n)
  {
    const char c = s[n];
    const int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!ok)
      return 0;
  }
  return n == 64u;
}


void oak_pkg_source_free(oak_allocator_t* a, oak_pkg_source_t* s)
{
  if (!s)
    return;
  oak_free(a, s->location, OAK_HERE);
  oak_free(a, s->tag, OAK_HERE);
  oak_free(a, s->rev, OAK_HERE);
  oak_free(a, s->sha256, OAK_HERE);
  memset(s, 0, sizeof *s);
}

int oak_pkg_source_parse_spec(oak_pkg_source_t* out,
                              oak_semver_req_t* out_req,
                              oak_allocator_t* a,
                              const char* spec,
                              char* err,
                              const usize err_cap)
{
  if (!out || !a || !spec)
    return -1;
  memset(out, 0, sizeof *out);
  out->strip = 1;
  if (out_req)
  {
    out_req->op = OAK_SEMVER_ANY;
    memset(&out_req->version, 0, sizeof out_req->version);
  }

  /* Local path: never fetched, never hashed, always exactly what is on disk. */
  if (strncmp(spec, "./", 2u) == 0 || strncmp(spec, "../", 3u) == 0 ||
      strncmp(spec, ".\\", 2u) == 0 || strncmp(spec, "..\\", 3u) == 0)
  {
    out->kind = OAK_PKG_SOURCE_PATH;
    out->location = oak_pkg_strdup(a, spec, OAK_HERE);
    return out->location ? 0 : -1;
  }

  const char* host = OAK_NULL;
  const char* body = OAK_NULL;
  if (strncmp(spec, "github:", 7u) == 0)
  {
    host = "https://github.com/";
    body = spec + 7;
  }
  else if (strncmp(spec, "gitlab:", 7u) == 0)
  {
    host = "https://gitlab.com/";
    body = spec + 7;
  }

  if (host)
  {
    /* owner/repo[@version] */
    const char* at = strrchr(body, '@');
    const usize repo_len = at ? (usize)(at - body) : strlen(body);
    if (repo_len == 0u || !memchr(body, '/', repo_len))
      return oak_pkg_fail(err, err_cap,
                          "'%s' must name owner/repo, e.g. "
                          "github:acme/oak-json@1.2.0",
                          spec);

    const usize host_len = strlen(host);
    /* +4 for the ".git" suffix git servers accept and humans omit. */
    char* url = oak_alloc(a, host_len + repo_len + 5u, OAK_HERE);
    if (!url)
      return -1;
    memcpy(url, host, host_len);
    memcpy(url + host_len, body, repo_len);
    memcpy(url + host_len + repo_len, ".git", 5u);

    out->kind = OAK_PKG_SOURCE_GIT;
    out->location = url;

    if (at)
    {
      const char* version = at + 1;
      if (!*version)
        return oak_pkg_fail(err, err_cap, "'%s' ends in '@' with no version",
                            spec);
      oak_semver_t parsed;
      if (oak_semver_parse(version, &parsed) != 0)
        return oak_pkg_fail(err, err_cap,
                            "'%s' is not a version like 1.2.0 in '%s'",
                            version, spec);
      /* Shorthand pins the tag but asks for a compatible range, so a
       * transitive dependent wanting 1.3.0 can raise it without an edit. */
      if (out_req)
      {
        out_req->op = OAK_SEMVER_CARET;
        out_req->version = parsed;
      }
      const usize vlen = strlen(version);
      char* tag = oak_alloc(a, vlen + 2u, OAK_HERE);
      if (!tag)
        return -1;
      tag[0] = 'v';
      memcpy(tag + 1, version, vlen + 1u);
      out->tag = tag;
    }
    return 0;
  }

  if (strncmp(spec, "https://", 8u) == 0)
  {
    /* A git URL and an archive URL are told apart by shape, since both are
     * plain https: anything ending in .git is a repository, everything else is
     * a file to download. An author who wants the other reading writes the
     * long form. */
    const usize len = strlen(spec);
    const int is_git = len > 4u && strcmp(spec + len - 4u, ".git") == 0;
    out->kind = is_git ? OAK_PKG_SOURCE_GIT : OAK_PKG_SOURCE_URL;
    out->location = oak_pkg_strdup(a, spec, OAK_HERE);
    return out->location ? 0 : -1;
  }

  /* A local repository, for a mirror or an air-gapped checkout. Always a git
   * source: file:// naming an archive has no advantage over a path
   * dependency, which is already the answer for something on this disk. */
  if (strncmp(spec, "file://", 7u) == 0)
  {
    out->kind = OAK_PKG_SOURCE_GIT;
    out->location = oak_pkg_strdup(a, spec, OAK_HERE);
    return out->location ? 0 : -1;
  }

  if (strncmp(spec, "http://", 7u) == 0)
    return oak_pkg_fail(err, err_cap,
                        "'%s' is plain http; package sources must be https",
                        spec);

  return oak_pkg_fail(err, err_cap,
                      "'%s' is not a dependency spec (expected "
                      "github:owner/repo@1.2.0, an https URL, or ./path)",
                      spec);
}

/* Long form: { "git": ..., "tag": ... } / { "url": ..., "sha256": ... } /
 * { "path": ... }. Exactly one of the three location keys must be present. */
static int parse_dep_object(oak_pkg_source_t* out,
                            oak_semver_req_t* out_req,
                            oak_allocator_t* a,
                            yyjson_val* obj,
                            const char* alias,
                            char* err,
                            const usize err_cap)
{
  memset(out, 0, sizeof *out);
  out->strip = 1;
  out_req->op = OAK_SEMVER_ANY;
  memset(&out_req->version, 0, sizeof out_req->version);

  const char* git = OAK_NULL;
  const char* url = OAK_NULL;
  const char* path = OAK_NULL;
  const char* tag = OAK_NULL;
  const char* rev = OAK_NULL;
  const char* sha256 = OAK_NULL;
  const char* version = OAK_NULL;

  if (obj_str(obj, "git", &git, err, err_cap, alias) != 0 ||
      obj_str(obj, "url", &url, err, err_cap, alias) != 0 ||
      obj_str(obj, "path", &path, err, err_cap, alias) != 0 ||
      obj_str(obj, "tag", &tag, err, err_cap, alias) != 0 ||
      obj_str(obj, "rev", &rev, err, err_cap, alias) != 0 ||
      obj_str(obj, "sha256", &sha256, err, err_cap, alias) != 0 ||
      obj_str(obj, "version", &version, err, err_cap, alias) != 0)
    return -1;

  const int locations = (git ? 1 : 0) + (url ? 1 : 0) + (path ? 1 : 0);
  if (locations == 0)
    return oak_pkg_fail(err, err_cap,
                        "dependency '%s' has no 'git', 'url' or 'path'", alias);
  if (locations > 1)
    return oak_pkg_fail(err, err_cap,
                        "dependency '%s' names more than one of 'git', 'url' "
                        "and 'path'",
                        alias);

  if (version && oak_semver_req_parse(version, out_req) != 0)
    return oak_pkg_fail(err, err_cap,
                        "dependency '%s': '%s' is not a version constraint "
                        "(expected *, 1.2.3, ^1.2.3 or >=1.2.3)",
                        alias, version);

  if (path)
  {
    out->kind = OAK_PKG_SOURCE_PATH;
    out->location = oak_pkg_strdup(a, path, OAK_HERE);
    return out->location ? 0 : -1;
  }

  if (git)
  {
    /* file:// is here for local mirrors and air-gapped setups, where the
     * repository is already on a filesystem the developer controls. It reaches
     * no network, so it weakens nothing the https/ssh rule is protecting: that
     * rule exists to stop a published package pulling code over a channel
     * nobody authenticated. */
    if (strncmp(git, "https://", 8u) != 0 && strncmp(git, "git@", 4u) != 0 &&
        strncmp(git, "ssh://", 6u) != 0 && strncmp(git, "file://", 7u) != 0)
      return oak_pkg_fail(err, err_cap,
                          "dependency '%s': git URL '%s' must be https, ssh or "
                          "file",
                          alias, git);
    if (tag && rev)
      return oak_pkg_fail(err, err_cap,
                          "dependency '%s' sets both 'tag' and 'rev'; pick one",
                          alias);
    if (!tag && !rev)
      return oak_pkg_fail(err, err_cap,
                          "dependency '%s' needs a 'tag' or 'rev'; an "
                          "unpinned branch is not reproducible",
                          alias);
    out->kind = OAK_PKG_SOURCE_GIT;
    out->location = oak_pkg_strdup(a, git, OAK_HERE);
    out->tag = oak_pkg_strdup(a, tag, OAK_HERE);
    out->rev = oak_pkg_strdup(a, rev, OAK_HERE);
    return out->location ? 0 : -1;
  }

  if (strncmp(url, "https://", 8u) != 0)
    return oak_pkg_fail(err, err_cap,
                        "dependency '%s': archive URL '%s' must be https",
                        alias, url);
  /* sha256 may be absent -- `oak-pkg add` records it on first fetch -- but a
   * present one has to be a real digest, or verification would pass on a
   * typo. */
  if (sha256 && !is_hex64(sha256))
    return oak_pkg_fail(err, err_cap,
                        "dependency '%s': sha256 must be 64 lowercase hex "
                        "characters",
                        alias);
  out->kind = OAK_PKG_SOURCE_URL;
  out->location = oak_pkg_strdup(a, url, OAK_HERE);
  out->sha256 = oak_pkg_strdup(a, sha256, OAK_HERE);
  if (obj_int(obj, "strip", &out->strip, 1, err, err_cap, alias) != 0)
    return -1;
  if (out->strip < 0)
    return oak_pkg_fail(err, err_cap,
                        "dependency '%s': 'strip' cannot be negative", alias);
  return out->location ? 0 : -1;
}

static int parse_deps(oak_pkg_manifest_t* m,
                      yyjson_val* deps,
                      char* err,
                      const usize err_cap)
{
  if (!deps || yyjson_is_null(deps))
    return 0;
  if (!yyjson_is_obj(deps))
    return oak_pkg_fail(err, err_cap, "'deps' must be an object");

  yyjson_val* key;
  yyjson_val* val;
  yyjson_obj_iter iter;
  yyjson_obj_iter_init(deps, &iter);
  while ((key = yyjson_obj_iter_next(&iter)) != OAK_NULL)
  {
    val = yyjson_obj_iter_get_val(key);
    const char* alias = yyjson_get_str(key);
    if (!alias || !alias[0])
      return oak_pkg_fail(err, err_cap, "a dependency has an empty name");
    /* The alias becomes the first segment of an import, so it has to be a
     * single identifier-shaped word -- a dotted alias would claim a namespace
     * it does not own. */
    if (strchr(alias, '.') || strchr(alias, '/'))
      return oak_pkg_fail(err, err_cap,
                          "dependency name '%s' must be a single import "
                          "segment, with no '.' or '/'",
                          alias);

    oak_pkg_dep_t dep;
    memset(&dep, 0, sizeof dep);

    if (yyjson_is_str(val))
    {
      if (oak_pkg_source_parse_spec(&dep.source, &dep.req, m->allocator,
                                    yyjson_get_str(val), err, err_cap) != 0)
      {
        oak_pkg_source_free(m->allocator, &dep.source);
        return -1;
      }
    }
    else if (yyjson_is_obj(val))
    {
      if (parse_dep_object(&dep.source, &dep.req, m->allocator, val, alias, err,
                           err_cap) != 0)
      {
        oak_pkg_source_free(m->allocator, &dep.source);
        return -1;
      }
    }
    else
    {
      return oak_pkg_fail(err, err_cap,
                          "dependency '%s' must be a spec string or an object",
                          alias);
    }

    dep.alias = oak_pkg_strdup(m->allocator, alias, OAK_HERE);
    if (!dep.alias)
    {
      oak_pkg_source_free(m->allocator, &dep.source);
      return -1;
    }
    OAK_ASSERT(oak_push_back(m->deps, &dep));
  }
  return 0;
}

static int parse_native(oak_pkg_manifest_t* m,
                        yyjson_val* native,
                        char* err,
                        const usize err_cap)
{
  if (!native || yyjson_is_null(native))
    return 0;
  if (!yyjson_is_obj(native))
    return oak_pkg_fail(err, err_cap, "'native' must be an object");

  const char* lib = OAK_NULL;
  const char* dir = OAK_NULL;
  if (obj_str(native, "lib", &lib, err, err_cap, "native") != 0 ||
      obj_str(native, "dir", &dir, err, err_cap, "native") != 0)
    return -1;
  if (!lib || !lib[0])
    return oak_pkg_fail(err, err_cap,
                        "'native' needs a 'lib' naming the shared library");
  if (obj_int(native, "abi", &m->native.abi, 0, err, err_cap, "native") != 0)
    return -1;
  if (m->native.abi <= 0)
    return oak_pkg_fail(err, err_cap,
                        "'native' needs a positive 'abi' (the plugin ABI it "
                        "was built against)");

  m->native.lib = oak_pkg_strdup(m->allocator, lib, OAK_HERE);
  m->native.dir = oak_pkg_strdup(m->allocator, dir ? dir : "native", OAK_HERE);
  if (!m->native.lib || !m->native.dir)
    return -1;
  m->has_native = 1;
  return 0;
}

int oak_pkg_manifest_parse(oak_pkg_manifest_t* out,
                           oak_allocator_t* a,
                           const char* json,
                           const usize len,
                           char* err,
                           const usize err_cap)
{
  if (!out || !a || !json)
    return -1;
  memset(out, 0, sizeof *out);
  out->allocator = a;
  out->deps = oak_vector_new(a, sizeof(oak_pkg_dep_t));
  if (!out->deps)
    return -1;

  yyjson_read_err rerr;
  memset(&rerr, 0, sizeof rerr);
  yyjson_doc* doc =
      yyjson_read_opts((char*)json, len, 0, OAK_NULL, &rerr);
  if (!doc)
  {
    oak_pkg_fail(err, err_cap, "invalid JSON at byte %zu: %s",
                 (usize)rerr.pos, rerr.msg ? rerr.msg : "parse error");
    oak_pkg_manifest_free(out);
    return -1;
  }

  yyjson_val* root = yyjson_doc_get_root(doc);
  int rc = -1;
  do
  {
    if (!yyjson_is_obj(root))
    {
      oak_pkg_fail(err, err_cap, "the manifest must be a JSON object");
      break;
    }

    const char* name = OAK_NULL;
    const char* version = OAK_NULL;
    const char* module = OAK_NULL;
    const char* src = OAK_NULL;
    const char* license = OAK_NULL;
    const char* oak_req = OAK_NULL;
    if (obj_str(root, "name", &name, err, err_cap, "manifest") != 0 ||
        obj_str(root, "version", &version, err, err_cap, "manifest") != 0 ||
        obj_str(root, "module", &module, err, err_cap, "manifest") != 0 ||
        obj_str(root, "src", &src, err, err_cap, "manifest") != 0 ||
        obj_str(root, "license", &license, err, err_cap, "manifest") != 0 ||
        obj_str(root, "oak", &oak_req, err, err_cap, "manifest") != 0)
      break;

    if (!name || !name[0])
    {
      oak_pkg_fail(err, err_cap, "the manifest needs a 'name'");
      break;
    }
    if (!version)
    {
      oak_pkg_fail(err, err_cap, "package '%s' needs a 'version'", name);
      break;
    }
    if (oak_semver_parse(version, &out->version) != 0)
    {
      oak_pkg_fail(err, err_cap,
                   "package '%s': '%s' is not a version like 1.2.0", name,
                   version);
      break;
    }

    /* The import namespace defaults to the name without its owner, so
     * "acme/json" is imported as `json` and most manifests never write it. */
    if (!module)
      module = name_tail(name);
    if (strchr(module, '.') || strchr(module, '/'))
    {
      oak_pkg_fail(err, err_cap,
                   "package '%s': module '%s' must be a single import "
                   "segment, with no '.' or '/'",
                   name, module);
      break;
    }

    out->oak_req.op = OAK_SEMVER_ANY;
    if (oak_req && oak_semver_req_parse(oak_req, &out->oak_req) != 0)
    {
      oak_pkg_fail(err, err_cap,
                   "package '%s': '%s' is not a version constraint for 'oak'",
                   name, oak_req);
      break;
    }

    out->name = oak_pkg_strdup(a, name, OAK_HERE);
    out->module = oak_pkg_strdup(a, module, OAK_HERE);
    out->src = oak_pkg_strdup(a, src && src[0] ? src : ".", OAK_HERE);
    out->license = oak_pkg_strdup(a, license, OAK_HERE);
    if (!out->name || !out->module || !out->src)
      break;

    if (parse_deps(out, yyjson_obj_get(root, "deps"), err, err_cap) != 0)
      break;
    if (parse_native(out, yyjson_obj_get(root, "native"), err, err_cap) != 0)
      break;

    rc = 0;
  } while (0);

  yyjson_doc_free(doc);
  if (rc != 0)
    oak_pkg_manifest_free(out);
  return rc;
}

int oak_pkg_manifest_read(oak_pkg_manifest_t* out,
                          oak_allocator_t* a,
                          const char* path,
                          char* err,
                          const usize err_cap)
{
  if (!out || !a || !path)
    return -1;
  memset(out, 0, sizeof *out);

  usize len = 0;
  char* text = oak_pkg_read_file(a, path, &len, err, err_cap, OAK_HERE);
  if (!text)
    return -1;

  const int rc = oak_pkg_manifest_parse(out, a, text, len, err, err_cap);
  oak_free(a, text, OAK_HERE);

  /* A syntax error in a file the author can open is only useful with the file
   * named, and the parser only ever sees the bytes. */
  if (rc != 0 && err && err_cap > 0u)
  {
    char detail[OAK_PKG_ERROR_MAX];
    snprintf(detail, sizeof detail, "%s", err);
    snprintf(err, err_cap, "%s: %s", path, detail);
  }
  return rc;
}

int oak_pkg_manifest_read_dir(oak_pkg_manifest_t* out,
                              oak_allocator_t* a,
                              const char* dir,
                              const char* what,
                              char* err,
                              const usize err_cap)
{
  if (!out || !a || !dir)
    return -1;

  char* path = oak_pkg_path_join(a, dir, "oak.json", OAK_HERE);
  if (!path)
    return -1;

  /* Distinguish "not a package" from "a package with a broken manifest".
   * Everything reaching here was named as a dependency, so a missing manifest
   * almost always means the wrong URL, tag or subdirectory. */
  FILE* probe = fopen(path, "rb");
  if (!probe)
  {
    oak_pkg_fail(err, err_cap, "%s is not an Oak package: there is no oak.json",
                 what ? what : dir);
    oak_free(a, path, OAK_HERE);
    return -1;
  }
  fclose(probe);

  const int rc = oak_pkg_manifest_read(out, a, path, err, err_cap);
  oak_free(a, path, OAK_HERE);
  return rc;
}

void oak_pkg_manifest_free(oak_pkg_manifest_t* m)
{
  if (!m || !m->allocator)
    return;
  oak_allocator_t* a = m->allocator;

  if (m->deps)
  {
    oak_pkg_dep_t* deps = OAK_DATA(oak_pkg_dep_t, m->deps);
    for (usize i = 0; i < oak_size(m->deps); ++i)
    {
      oak_free(a, deps[i].alias, OAK_HERE);
      oak_pkg_source_free(a, &deps[i].source);
    }
    oak_destroy(m->deps);
  }
  oak_free(a, m->name, OAK_HERE);
  oak_free(a, m->module, OAK_HERE);
  oak_free(a, m->src, OAK_HERE);
  oak_free(a, m->license, OAK_HERE);
  oak_free(a, m->native.lib, OAK_HERE);
  oak_free(a, m->native.dir, OAK_HERE);
  memset(m, 0, sizeof *m);
}

const oak_pkg_dep_t* oak_pkg_manifest_dep(const oak_pkg_manifest_t* m,
                                          const char* alias)
{
  if (!m || !m->deps || !alias)
    return OAK_NULL;
  const oak_pkg_dep_t* deps = OAK_CDATA(oak_pkg_dep_t, m->deps);
  for (usize i = 0; i < oak_size(m->deps); ++i)
    if (strcmp(deps[i].alias, alias) == 0)
      return &deps[i];
  return OAK_NULL;
}
