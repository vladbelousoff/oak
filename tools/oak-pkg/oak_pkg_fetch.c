/*
 * Getting a dependency onto disk, once.
 *
 * Everything here is arranged around one property: a cache directory is named
 * after what is inside it -- a commit, or an archive's digest -- so a directory
 * that exists is already known to be the right bytes.  Nothing is ever
 * invalidated, two projects share one copy, and a fetch that is interrupted
 * leaves nothing behind, because work happens in a staging directory and is
 * renamed into place only once it is complete.
 *
 * The awkward case is a tag: the cache name depends on the commit, and the
 * commit is only known after cloning.  So a tag clones into staging first and
 * the destination is computed afterwards -- and if another run got there in the
 * meantime, the staged copy is simply thrown away.
 */

#include "oak_pkg_tool.h"

#include "internal/oak_pkg_util.h"

#include "oak_pkg_cache.h"
#include "oak_pkg_fs.h"
#include "oak_pkg_git.h"
#include "oak_pkg_sha256.h"

#include <stdio.h>

/* One tree this process has already put in the cache. */
typedef struct oak_pkg_memo oak_pkg_memo_t;
struct oak_pkg_memo
{
  char* key;
  char* dir;
  char* rev;
  char* sha256;
};

/* What identifies a fetch: where from, and whatever pins it. */
static char* memo_key(oak_pkg_tool_t* t, const oak_pkg_source_t* s)
{
  const char* pin =
      s->rev ? s->rev : (s->tag ? s->tag : (s->sha256 ? s->sha256 : ""));
  const usize n = strlen(s->location) + strlen(pin) + 2u;
  char* key = oak_alloc(t->a, n, OAK_HERE);
  if (key)
    snprintf(key, n, "%s@%s", s->location, pin);
  return key;
}

/* Answer from the memo if this exact source has already been fetched. */
static int recall(oak_pkg_tool_t* t,
                  const oak_pkg_source_t* source,
                  oak_pkg_fetched_t* out)
{
  if (!t->fetched)
    return 0;
  char* key = memo_key(t, source);
  if (!key)
    return 0;

  int hit = 0;
  const oak_pkg_memo_t* m = OAK_CDATA(oak_pkg_memo_t, t->fetched);
  for (usize i = 0; i < oak_size(t->fetched); ++i)
    if (strcmp(m[i].key, key) == 0)
    {
      out->dir = oak_pkg_strdup(t->a, m[i].dir, OAK_HERE);
      out->rev = oak_pkg_strdup(t->a, m[i].rev, OAK_HERE);
      out->sha256 = oak_pkg_strdup(t->a, m[i].sha256, OAK_HERE);
      hit = out->dir != OAK_NULL;
      break;
    }
  oak_free(t->a, key, OAK_HERE);
  return hit;
}

/* Note a completed fetch. Failing to record one costs a redundant clone, never
 * correctness, so an allocation failure here is not worth propagating. */
static void remember(oak_pkg_tool_t* t,
                     const oak_pkg_source_t* source,
                     const oak_pkg_fetched_t* got)
{
  /* Created on first use, so the memo's element type stays private to this
   * file rather than being something oak_pkg_tool_open has to know. */
  if (!t->fetched)
    t->fetched = oak_vector_new(t->a, sizeof(oak_pkg_memo_t));
  if (!t->fetched)
    return;
  oak_pkg_memo_t m;
  memset(&m, 0, sizeof m);
  m.key = memo_key(t, source);
  m.dir = oak_pkg_strdup(t->a, got->dir, OAK_HERE);
  m.rev = oak_pkg_strdup(t->a, got->rev, OAK_HERE);
  m.sha256 = oak_pkg_strdup(t->a, got->sha256, OAK_HERE);
  if (!m.key || !m.dir || !oak_push_back(t->fetched, &m))
  {
    oak_free(t->a, m.key, OAK_HERE);
    oak_free(t->a, m.dir, OAK_HERE);
    oak_free(t->a, m.rev, OAK_HERE);
    oak_free(t->a, m.sha256, OAK_HERE);
  }
}

void oak_pkg_tool_forget(oak_pkg_tool_t* t)
{
  if (!t || !t->fetched)
    return;
  oak_pkg_memo_t* m = OAK_DATA(oak_pkg_memo_t, t->fetched);
  for (usize i = 0; i < oak_size(t->fetched); ++i)
  {
    oak_free(t->a, m[i].key, OAK_HERE);
    oak_free(t->a, m[i].dir, OAK_HERE);
    oak_free(t->a, m[i].rev, OAK_HERE);
    oak_free(t->a, m[i].sha256, OAK_HERE);
  }
  oak_destroy(t->fetched);
  t->fetched = OAK_NULL;
}

/* What the previous lock pinned this location to, or null. Consulted so that
 * `install` reproduces the recorded commit instead of re-reading a tag that may
 * have been moved; `update` is the command that deliberately skips it. */
static const oak_pkg_lock_entry_t* pinned(const oak_pkg_tool_t* t,
                                          const oak_pkg_source_t* source)
{
  if (t->refresh || !t->have_lock)
    return OAK_NULL;
  return oak_pkg_lock_find(&t->lock, source->location);
}

/* Hand back a cache directory that already exists. */
static int already_have(oak_pkg_tool_t* t,
                        char* dir,
                        const char* rev,
                        const char* sha256,
                        oak_pkg_fetched_t* out)
{
  out->dir = dir;
  out->rev = oak_pkg_strdup(t->a, rev, OAK_HERE);
  out->sha256 = oak_pkg_strdup(t->a, sha256, OAK_HERE);
  return 0;
}

/* The directory fetches stage in, under the cache root so publishing is a
 * rename inside one filesystem. */
static char* make_stage(oak_pkg_tool_t* t, char* err, const usize err_cap)
{
  if (oak_pkg_mkdir_p(t->cache_root) != 0)
  {
    oak_pkg_fail(err, err_cap, "cannot create the cache at '%s'",
                 t->cache_root);
    return OAK_NULL;
  }
  char* stage = oak_pkg_stage_path(t->a, t->cache_root, OAK_HERE);
  if (!stage)
    oak_pkg_fail(err, err_cap, "cannot find a free staging directory in '%s'",
                 t->cache_root);
  return stage;
}

/* Move a completed staging directory to its content-addressed home, or discard
 * it if someone else finished the identical fetch first. */
static int publish(oak_pkg_tool_t* t,
                   char* stage,
                   char* final_dir,
                   char* err,
                   const usize err_cap)
{
  if (oak_pkg_is_dir(final_dir))
  {
    oak_pkg_rmtree(t->a, stage);
    return 0;
  }

  char* parent = oak_pkg_path_dirname(t->a, final_dir, OAK_HERE);
  const int made = parent ? oak_pkg_mkdir_p(parent) : -1;
  oak_free(t->a, parent, OAK_HERE);
  if (made != 0)
    return oak_pkg_fail(err, err_cap, "cannot create the cache directory for "
                                      "'%s'",
                        final_dir);

  if (oak_pkg_rename(stage, final_dir) != 0)
  {
    oak_pkg_rmtree(t->a, stage);
    return oak_pkg_fail(err, err_cap, "cannot move the download into '%s'",
                        final_dir);
  }
  return 0;
}

static int fetch_git(oak_pkg_tool_t* t,
                     const oak_pkg_source_t* source,
                     oak_pkg_fetched_t* out,
                     char* err,
                     const usize err_cap)
{
  const oak_pkg_lock_entry_t* lock = pinned(t, source);
  const char* known = source->rev ? source->rev : (lock ? lock->rev : OAK_NULL);

  if (known)
  {
    char* dir = oak_pkg_cache_dir(t->a, t->cache_root, source, known, OAK_NULL,
                                  OAK_HERE);
    if (!dir)
      return -1;
    if (oak_pkg_is_dir(dir))
      return already_have(t, dir, known, OAK_NULL, out);
    oak_free(t->a, dir, OAK_HERE);
  }

  if (t->offline)
    return oak_pkg_fail(err, err_cap,
                        "'%s' is not in the cache and OAK_OFFLINE is set",
                        source->location);

  /* A known commit is checked out directly; only a tag has to be resolved,
   * and only the first time. */
  oak_pkg_source_t pinned_source = *source;
  char* rev_copy = OAK_NULL;
  if (known && !source->rev)
  {
    rev_copy = oak_pkg_strdup(t->a, known, OAK_HERE);
    if (!rev_copy)
      return -1;
    pinned_source.rev = rev_copy;
    pinned_source.tag = OAK_NULL;
  }

  char* stage = make_stage(t, err, err_cap);
  if (!stage)
  {
    oak_free(t->a, rev_copy, OAK_HERE);
    return -1;
  }

  oak_pkg_say(t, "fetching %s%s%s", source->location,
              pinned_source.tag ? " at " : (pinned_source.rev ? " at " : ""),
              pinned_source.tag ? pinned_source.tag
                                : (pinned_source.rev ? pinned_source.rev : ""));

  char rev[OAK_PKG_REV_SIZE];
  const int rc =
      oak_pkg_git_fetch(t->a, &pinned_source, stage, rev, err, err_cap);
  oak_free(t->a, rev_copy, OAK_HERE);
  if (rc != 0)
  {
    oak_pkg_rmtree(t->a, stage);
    oak_free(t->a, stage, OAK_HERE);
    return -1;
  }

  char* final_dir =
      oak_pkg_cache_dir(t->a, t->cache_root, source, rev, OAK_NULL, OAK_HERE);
  if (!final_dir || publish(t, stage, final_dir, err, err_cap) != 0)
  {
    oak_free(t->a, stage, OAK_HERE);
    oak_free(t->a, final_dir, OAK_HERE);
    return -1;
  }
  oak_free(t->a, stage, OAK_HERE);

  out->dir = final_dir;
  out->rev = oak_pkg_strdup(t->a, rev, OAK_HERE);
  return out->rev ? 0 : -1;
}

static int fetch_url(oak_pkg_tool_t* t,
                     const oak_pkg_source_t* source,
                     oak_pkg_fetched_t* out,
                     char* err,
                     const usize err_cap)
{
  const oak_pkg_lock_entry_t* lock = pinned(t, source);
  const char* known =
      source->sha256 ? source->sha256 : (lock ? lock->sha256 : OAK_NULL);

  if (known)
  {
    char* dir = oak_pkg_cache_dir(t->a, t->cache_root, source, OAK_NULL, known,
                                  OAK_HERE);
    if (!dir)
      return -1;
    if (oak_pkg_is_dir(dir))
      return already_have(t, dir, OAK_NULL, known, out);
    oak_free(t->a, dir, OAK_HERE);
  }

  if (t->offline)
    return oak_pkg_fail(err, err_cap,
                        "'%s' is not in the cache and OAK_OFFLINE is set",
                        source->location);

  if (!oak_pkg_http_available())
    return oak_pkg_fail(err, err_cap,
                        "this oak-pkg was built without archive support, so it "
                        "cannot download '%s'; rebuild with libcurl and "
                        "libarchive, or use a git dependency",
                        source->location);

  char* stage = make_stage(t, err, err_cap);
  if (!stage)
    return -1;

  oak_pkg_say(t, "downloading %s", source->location);

  char digest[OAK_SHA256_HEX_SIZE];
  if (oak_pkg_http_fetch(t->a, source->location, known, source->strip, stage,
                         digest, err, err_cap) != 0)
  {
    oak_pkg_rmtree(t->a, stage);
    oak_free(t->a, stage, OAK_HERE);
    return -1;
  }

  char* final_dir = oak_pkg_cache_dir(t->a, t->cache_root, source, OAK_NULL,
                                      digest, OAK_HERE);
  if (!final_dir || publish(t, stage, final_dir, err, err_cap) != 0)
  {
    oak_free(t->a, stage, OAK_HERE);
    oak_free(t->a, final_dir, OAK_HERE);
    return -1;
  }
  oak_free(t->a, stage, OAK_HERE);

  /* Trust on first use, written down: nothing was pinned, so this run decides
   * what the digest is, and every later fetch is checked against it. */
  if (!known)
    oak_pkg_say(t, "  recorded sha256 %s", digest);

  out->dir = final_dir;
  out->sha256 = oak_pkg_strdup(t->a, digest, OAK_HERE);
  return out->sha256 ? 0 : -1;
}

int oak_pkg_tool_fetch(void* ctx,
                       const oak_pkg_source_t* source,
                       oak_pkg_fetched_t* out,
                       char* err,
                       const usize err_cap)
{
  oak_pkg_tool_t* t = (oak_pkg_tool_t*)ctx;
  if (!t || !source || !out)
    return -1;
  memset(out, 0, sizeof *out);

  if (!t->cache_root)
    return oak_pkg_fail(err, err_cap,
                        "cannot locate the package cache; set "
                        "OAK_PACKAGE_CACHE");

  if (recall(t, source, out))
    return 0;

  int rc = -1;
  if (source->kind == OAK_PKG_SOURCE_GIT)
    rc = fetch_git(t, source, out, err, err_cap);
  else if (source->kind == OAK_PKG_SOURCE_URL)
    rc = fetch_url(t, source, out, err, err_cap);
  else
    /* Path dependencies never reach here: the resolver handles them itself,
     * because there is nothing to fetch and nothing to record. */
    return oak_pkg_fail(err, err_cap, "'%s' is not a fetchable source",
                        source->location);

  if (rc == 0)
    remember(t, source, out);
  return rc;
}
