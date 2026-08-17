#include "oak_pkg_resolve.h"

#include "internal/oak_pkg_util.h"

#include "oak_log.h"

#include <stdio.h>

/* One package the resolver has decided on, pending emission into the lock. */
typedef struct cand cand_t;
struct cand
{
  char* name;
  oak_semver_t version;
  char* module;
  char* src;
  oak_pkg_source_kind_t kind;
  char* location;
  char* rev;
  char* sha256;
  int strip;
};

/* A dependency edge still to be examined. */
typedef struct work work_t;
struct work
{
  oak_pkg_source_t source;
  oak_semver_req_t req;
  /* Directory a relative path source resolves against.  Owned. */
  char* base_dir;
  /* Who asked, for error messages.  Owned. */
  char* from;
};

/* A tree already materialized this run, so the same dependency reached twice
 * is cloned once. */
typedef struct memo memo_t;
struct memo
{
  char* key;
  char* dir;
  char* rev;
  char* sha256;
};

typedef struct resolver resolver_t;
struct resolver
{
  oak_allocator_t* a;
  oak_pkg_fetch_fn fetch;
  void* ctx;
  oak_container_t* cands;  /* cand_t   */
  oak_container_t* queue;  /* work_t   */
  oak_container_t* memos;  /* memo_t   */
  oak_container_t* walked; /* char*, absolute dirs of walked path deps */
  char* err;
  usize err_cap;
};

static char* dup_str(resolver_t* r, const char* s)
{
  return oak_pkg_strdup(r->a, s, OAK_HERE);
}

static void source_release(oak_allocator_t* a, oak_pkg_source_t* s)
{
  oak_free(a, s->location, OAK_HERE);
  oak_free(a, s->tag, OAK_HERE);
  oak_free(a, s->rev, OAK_HERE);
  oak_free(a, s->sha256, OAK_HERE);
  memset(s, 0, sizeof *s);
}

static int source_copy(resolver_t* r,
                       oak_pkg_source_t* dst,
                       const oak_pkg_source_t* src)
{
  memset(dst, 0, sizeof *dst);
  dst->kind = src->kind;
  dst->strip = src->strip;
  if (src->location && !(dst->location = dup_str(r, src->location)))
    return -1;
  if (src->tag && !(dst->tag = dup_str(r, src->tag)))
    return -1;
  if (src->rev && !(dst->rev = dup_str(r, src->rev)))
    return -1;
  if (src->sha256 && !(dst->sha256 = dup_str(r, src->sha256)))
    return -1;
  return 0;
}

static void work_release(oak_allocator_t* a, work_t* w)
{
  source_release(a, &w->source);
  oak_free(a, w->base_dir, OAK_HERE);
  oak_free(a, w->from, OAK_HERE);
}

static void cand_release(oak_allocator_t* a, cand_t* c)
{
  oak_free(a, c->name, OAK_HERE);
  oak_free(a, c->module, OAK_HERE);
  oak_free(a, c->src, OAK_HERE);
  oak_free(a, c->location, OAK_HERE);
  oak_free(a, c->rev, OAK_HERE);
  oak_free(a, c->sha256, OAK_HERE);
  memset(c, 0, sizeof *c);
}

/* Enqueue every dependency of `m`, to be resolved against `base_dir`. */
static int enqueue_deps(resolver_t* r,
                        const oak_pkg_manifest_t* m,
                        const char* base_dir,
                        const char* from)
{
  const oak_pkg_dep_t* deps = OAK_CDATA(oak_pkg_dep_t, m->deps);
  for (usize i = 0; i < oak_size(m->deps); ++i)
  {
    work_t w;
    memset(&w, 0, sizeof w);
    w.req = deps[i].req;
    if (source_copy(r, &w.source, &deps[i].source) != 0 ||
        !(w.base_dir = dup_str(r, base_dir)) ||
        !(w.from = dup_str(r, from ? from : "the project")))
    {
      work_release(r->a, &w);
      return -1;
    }
    if (!oak_push_back(r->queue, &w))
    {
      work_release(r->a, &w);
      return -1;
    }
  }
  return 0;
}

/* Read the manifest in `dir`, prefixing any complaint with which package it
 * belongs to rather than leaving a bare filename. */
static int read_manifest_in(resolver_t* r,
                            const char* dir,
                            const char* label,
                            oak_pkg_manifest_t* out)
{
  return oak_pkg_manifest_read_dir(out, r->a, dir, label, r->err, r->err_cap);
}

/* Two versions are interchangeable only within a compatible range. Below 1.0.0
 * the minor carries that role, matching how a caret constraint reads it. */
static int compatible(const oak_semver_t* a, const oak_semver_t* b)
{
  if (a->major != b->major)
    return 0;
  if (a->major == 0u && a->minor != b->minor)
    return 0;
  return 1;
}

static cand_t* find_cand(resolver_t* r, const char* name)
{
  cand_t* c = OAK_DATA(cand_t, r->cands);
  for (usize i = 0; i < oak_size(r->cands); ++i)
    if (strcmp(c[i].name, name) == 0)
      return &c[i];
  return OAK_NULL;
}

/* The identity of a fetch: the location plus whatever pins it. */
static char* memo_key(resolver_t* r, const oak_pkg_source_t* s)
{
  const char* pin = s->rev ? s->rev : (s->tag ? s->tag : (s->sha256 ? s->sha256 : ""));
  const usize n = strlen(s->location) + strlen(pin) + 2u;
  char* key = oak_alloc(r->a, n, OAK_HERE);
  if (!key)
    return OAK_NULL;
  snprintf(key, n, "%s@%s", s->location, pin);
  return key;
}

static const memo_t* find_memo(resolver_t* r, const char* key)
{
  const memo_t* m = OAK_CDATA(memo_t, r->memos);
  for (usize i = 0; i < oak_size(r->memos); ++i)
    if (strcmp(m[i].key, key) == 0)
      return &m[i];
  return OAK_NULL;
}

/* Materialize `source`, reusing this run's earlier fetch of the same tree. */
static int fetch_once(resolver_t* r,
                      const oak_pkg_source_t* source,
                      const char** out_dir,
                      const char** out_rev,
                      const char** out_sha256)
{
  char* key = memo_key(r, source);
  if (!key)
    return -1;

  const memo_t* hit = find_memo(r, key);
  if (hit)
  {
    oak_free(r->a, key, OAK_HERE);
    *out_dir = hit->dir;
    *out_rev = hit->rev;
    *out_sha256 = hit->sha256;
    return 0;
  }

  oak_pkg_fetched_t got;
  memset(&got, 0, sizeof got);
  if (r->fetch(r->ctx, source, &got, r->err, r->err_cap) != 0)
  {
    oak_free(r->a, key, OAK_HERE);
    return -1;
  }

  memo_t m;
  memset(&m, 0, sizeof m);
  m.key = key;
  m.dir = got.dir;
  m.rev = got.rev;
  m.sha256 = got.sha256;
  if (!oak_push_back(r->memos, &m))
  {
    oak_free(r->a, m.key, OAK_HERE);
    oak_free(r->a, m.dir, OAK_HERE);
    oak_free(r->a, m.rev, OAK_HERE);
    oak_free(r->a, m.sha256, OAK_HERE);
    return -1;
  }

  const memo_t* stored = OAK_CDATA(memo_t, r->memos) + (oak_size(r->memos) - 1u);
  *out_dir = stored->dir;
  *out_rev = stored->rev;
  *out_sha256 = stored->sha256;
  return 0;
}

/* Walk a local dependency: its own dependencies join the graph, but it
 * contributes no lock entry of its own. */
static int visit_path(resolver_t* r, const work_t* w)
{
  char* joined = oak_pkg_path_join(r->a, w->base_dir, w->source.location, OAK_HERE);
  if (!joined)
    return -1;
  char* dir = oak_pkg_path_abs(r->a, joined, OAK_HERE);
  oak_free(r->a, joined, OAK_HERE);
  if (!dir)
    return oak_pkg_fail(r->err, r->err_cap,
                        "%s: path dependency '%s' does not exist", w->from,
                        w->source.location);

  const char** seen = OAK_CDATA(char*, r->walked);
  for (usize i = 0; i < oak_size(r->walked); ++i)
    if (strcmp(seen[i], dir) == 0)
    {
      oak_free(r->a, dir, OAK_HERE);
      return 0;
    }

  oak_pkg_manifest_t m;
  if (read_manifest_in(r, dir, w->source.location, &m) != 0)
  {
    oak_free(r->a, dir, OAK_HERE);
    return -1;
  }

  const int rc = enqueue_deps(r, &m, dir, m.name);
  oak_pkg_manifest_free(&m);
  if (rc != 0 || !oak_push_back(r->walked, &dir))
  {
    oak_free(r->a, dir, OAK_HERE);
    return -1;
  }
  return 0;
}

/* Consider a fetched dependency for selection. */
static int visit_remote(resolver_t* r, const work_t* w)
{
  const char* dir = OAK_NULL;
  const char* rev = OAK_NULL;
  const char* sha256 = OAK_NULL;
  if (fetch_once(r, &w->source, &dir, &rev, &sha256) != 0)
    return -1;

  oak_pkg_manifest_t m;
  if (read_manifest_in(r, dir, w->source.location, &m) != 0)
    return -1;

  char have[64];
  oak_semver_format(&m.version, have, sizeof have);

  if (!oak_semver_req_match(&w->req, &m.version))
  {
    const int rc = oak_pkg_fail(r->err, r->err_cap,
                                "%s asks for a version of '%s' that '%s' is "
                                "not: it is %s",
                                w->from, m.name, w->source.location, have);
    oak_pkg_manifest_free(&m);
    return rc;
  }

  cand_t* existing = find_cand(r, m.name);
  if (existing)
  {
    /* Oak gives every module its own type identities, so two majors of one
     * package are two incompatible sets of types wearing the same names.
     * Loading both would produce errors nobody could act on. */
    if (!compatible(&existing->version, &m.version))
    {
      char chosen[64];
      oak_semver_format(&existing->version, chosen, sizeof chosen);
      const int rc = oak_pkg_fail(
          r->err, r->err_cap,
          "'%s' is needed at both %s and %s, which are not compatible; one "
          "of them has to move",
          m.name, chosen, have);
      oak_pkg_manifest_free(&m);
      return rc;
    }
    /* Minimal version selection: the highest minimum anyone asked for wins,
     * and anything lower has already been satisfied by it. */
    if (oak_semver_cmp(&m.version, &existing->version) <= 0)
    {
      oak_pkg_manifest_free(&m);
      return 0;
    }
    cand_release(r->a, existing);
  }

  cand_t fresh;
  memset(&fresh, 0, sizeof fresh);
  fresh.version = m.version;
  fresh.kind = w->source.kind;
  fresh.strip = w->source.strip;
  fresh.name = dup_str(r, m.name);
  fresh.module = dup_str(r, m.module);
  fresh.src = dup_str(r, m.src);
  fresh.location = dup_str(r, w->source.location);
  fresh.rev = rev ? dup_str(r, rev) : OAK_NULL;
  fresh.sha256 = sha256 ? dup_str(r, sha256) : OAK_NULL;

  const int allocated = fresh.name && fresh.module && fresh.src &&
                        fresh.location &&
                        (w->source.kind != OAK_PKG_SOURCE_GIT || fresh.rev) &&
                        (w->source.kind != OAK_PKG_SOURCE_URL || fresh.sha256);
  if (!allocated)
  {
    cand_release(r->a, &fresh);
    oak_pkg_manifest_free(&m);
    return -1;
  }

  if (existing)
    *existing = fresh;
  else if (!oak_push_back(r->cands, &fresh))
  {
    cand_release(r->a, &fresh);
    oak_pkg_manifest_free(&m);
    return -1;
  }

  const int rc = enqueue_deps(r, &m, dir, m.name);
  oak_pkg_manifest_free(&m);
  return rc;
}

/* Emit the selected packages, name-ordered so the lockfile is stable and a diff
 * shows what changed rather than how the walk happened to run. */
static int emit(resolver_t* r, oak_pkg_lock_t* out)
{
  cand_t* c = OAK_DATA(cand_t, r->cands);
  const usize n = oak_size(r->cands);
  for (usize i = 1; i < n; ++i)
  {
    const cand_t key = c[i];
    usize j = i;
    while (j > 0 && strcmp(c[j - 1u].name, key.name) > 0)
    {
      c[j] = c[j - 1u];
      --j;
    }
    c[j] = key;
  }

  for (usize i = 0; i < n; ++i)
  {
    oak_pkg_lock_entry_t e;
    memset(&e, 0, sizeof e);
    e.version = c[i].version;
    e.kind = c[i].kind;
    e.strip = c[i].strip;
    /* Ownership moves into the lock, which is released as one thing. */
    e.name = c[i].name;
    e.module = c[i].module;
    e.src = c[i].src;
    e.location = c[i].location;
    e.rev = c[i].rev;
    e.sha256 = c[i].sha256;
    memset(&c[i], 0, sizeof c[i]);
    if (!oak_push_back(out->entries, &e))
      return -1;
  }
  return 0;
}

int oak_pkg_resolve(oak_allocator_t* a,
                    const oak_pkg_manifest_t* root,
                    const char* root_dir,
                    oak_pkg_fetch_fn fetch,
                    void* ctx,
                    oak_pkg_lock_t* out,
                    char* err,
                    const usize err_cap)
{
  if (!a || !root || !root_dir || !fetch || !out)
    return -1;

  memset(out, 0, sizeof *out);
  out->allocator = a;
  out->entries = oak_vector_new(a, sizeof(oak_pkg_lock_entry_t));
  if (!out->entries)
    return -1;

  resolver_t r;
  memset(&r, 0, sizeof r);
  r.a = a;
  r.fetch = fetch;
  r.ctx = ctx;
  r.err = err;
  r.err_cap = err_cap;
  r.cands = oak_vector_new(a, sizeof(cand_t));
  r.queue = oak_vector_new(a, sizeof(work_t));
  r.memos = oak_vector_new(a, sizeof(memo_t));
  r.walked = oak_vector_new(a, sizeof(char*));

  int rc = -1;
  if (r.cands && r.queue && r.memos && r.walked &&
      enqueue_deps(&r, root, root_dir, root->name) == 0)
  {
    rc = 0;
    /* Index-based: visiting an item appends more, and the vector may move. */
    for (usize i = 0; i < oak_size(r.queue); ++i)
    {
      const work_t* w = OAK_CDATA(work_t, r.queue) + i;
      const int is_path = w->source.kind == OAK_PKG_SOURCE_PATH;
      /* Copy the item out: visiting appends to the queue, which can reallocate
       * it out from under a borrowed pointer. */
      work_t item;
      memset(&item, 0, sizeof item);
      item.req = w->req;
      if (source_copy(&r, &item.source, &w->source) != 0 ||
          !(item.base_dir = dup_str(&r, w->base_dir)) ||
          !(item.from = dup_str(&r, w->from)))
      {
        work_release(a, &item);
        rc = -1;
        break;
      }

      rc = is_path ? visit_path(&r, &item) : visit_remote(&r, &item);
      work_release(a, &item);
      if (rc != 0)
        break;
    }
  }

  if (rc == 0)
    rc = emit(&r, out);

  if (r.queue)
  {
    work_t* w = OAK_DATA(work_t, r.queue);
    for (usize i = 0; i < oak_size(r.queue); ++i)
      work_release(a, &w[i]);
    oak_destroy(r.queue);
  }
  if (r.cands)
  {
    cand_t* c = OAK_DATA(cand_t, r.cands);
    for (usize i = 0; i < oak_size(r.cands); ++i)
      cand_release(a, &c[i]);
    oak_destroy(r.cands);
  }
  if (r.memos)
  {
    memo_t* m = OAK_DATA(memo_t, r.memos);
    for (usize i = 0; i < oak_size(r.memos); ++i)
    {
      oak_free(a, m[i].key, OAK_HERE);
      oak_free(a, m[i].dir, OAK_HERE);
      oak_free(a, m[i].rev, OAK_HERE);
      oak_free(a, m[i].sha256, OAK_HERE);
    }
    oak_destroy(r.memos);
  }
  if (r.walked)
  {
    char** d = OAK_DATA(char*, r.walked);
    for (usize i = 0; i < oak_size(r.walked); ++i)
      oak_free(a, d[i], OAK_HERE);
    oak_destroy(r.walked);
  }

  if (rc != 0)
    oak_pkg_lock_free(out);
  return rc;
}
