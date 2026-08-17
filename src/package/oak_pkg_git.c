#include "oak_pkg_git.h"

#include "internal/oak_pkg_util.h"

#include "oak_pkg_fs.h"
#include "oak_pkg_proc.h"

#include <stdio.h>
#include <stdlib.h>

const char* oak_pkg_git_program(void)
{
  const char* override = getenv("OAK_GIT");
  return (override && override[0]) ? override : "git";
}

int oak_pkg_git_is_rev(const char* s)
{
  if (!s)
    return 0;
  usize n = 0;
  for (; s[n]; ++n)
  {
    const char c = s[n];
    const int hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F');
    if (!hex)
      return 0;
  }
  return n == 40u;
}

/* Two commit hashes, compared the way git writes them: hex, either case. */
static int rev_equal(const char* a, const char* b)
{
  usize i = 0;
  for (; a[i] && b[i]; ++i)
  {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'F')
      ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'F')
      cb = (char)(cb - 'A' + 'a');
    if (ca != cb)
      return 0;
  }
  return a[i] == 0 && b[i] == 0;
}

/* Run git and treat a non-zero exit as a failure described by `what`. */
static int run_git(oak_allocator_t* a,
                   const char* const* argv,
                   const int capture,
                   oak_pkg_proc_result_t* out,
                   const char* what,
                   char* err,
                   const usize err_cap)
{
  oak_pkg_proc_result_t local;
  memset(&local, 0, sizeof local);
  oak_pkg_proc_result_t* res = out ? out : &local;

  if (oak_pkg_proc_run(a, argv, OAK_NULL, capture, res, err, err_cap) != 0)
    return -1;

  if (res->exit_code != 0)
  {
    const int rc = oak_pkg_fail(err, err_cap, "%s (git exited %d)", what,
                                res->exit_code);
    oak_pkg_proc_result_free(a, res);
    return rc;
  }
  if (!out)
    oak_pkg_proc_result_free(a, res);
  return 0;
}

/* Shallow-clone the tag. --branch takes a tag as happily as a branch, and one
 * commit is all a package ever needs. */
static int clone_tag(oak_allocator_t* a,
                     const char* url,
                     const char* tag,
                     const char* dest,
                     char* err,
                     const usize err_cap)
{
  const char* argv[] = {
    oak_pkg_git_program(), "clone",  "--quiet", "--depth", "1",
    "--config",            "advice.detachedHead=false",
    "--branch",            tag,      "--",      url,       dest,
    OAK_NULL,
  };
  char what[OAK_PKG_ERROR_MAX];
  snprintf(what, sizeof what, "cannot clone '%s' at tag '%s'", url, tag);
  return run_git(a, argv, 0, OAK_NULL, what, err, err_cap);
}

/* Fetch one exact commit. Servers may refuse to serve an arbitrary object
 * (uploadpack.allowReachableSHA1InWant is off by default), so fall back to
 * fetching everything and checking the commit out from that. */
static int fetch_rev(oak_allocator_t* a,
                     const char* url,
                     const char* rev,
                     const char* dest,
                     char* err,
                     const usize err_cap)
{
  const char* const git = oak_pkg_git_program();

  if (oak_pkg_mkdir_p(dest) != 0)
    return oak_pkg_fail(err, err_cap, "cannot create '%s'", dest);

  const char* init_argv[] = { git, "init", "--quiet", dest, OAK_NULL };
  if (run_git(a, init_argv, 0, OAK_NULL, "cannot create a git repository", err,
              err_cap) != 0)
    return -1;

  const char* remote_argv[] = { git,      "-C",  dest, "remote",
                                "add",    "origin", "--", url,
                                OAK_NULL };
  if (run_git(a, remote_argv, 0, OAK_NULL, "cannot configure the git remote",
              err, err_cap) != 0)
    return -1;

  const char* shallow_argv[] = { git,    "-C",     dest, "fetch",
                                 "--quiet", "--depth", "1",  "origin",
                                 rev,    OAK_NULL };
  oak_pkg_proc_result_t shallow;
  memset(&shallow, 0, sizeof shallow);
  if (oak_pkg_proc_run(a, shallow_argv, OAK_NULL, 0, &shallow, err, err_cap) !=
      0)
    return -1;
  const int shallow_ok = shallow.exit_code == 0;
  oak_pkg_proc_result_free(a, &shallow);

  if (!shallow_ok)
  {
    const char* full_argv[] = { git,      "-C", dest, "fetch", "--quiet",
                                "origin", OAK_NULL };
    char what[OAK_PKG_ERROR_MAX];
    snprintf(what, sizeof what, "cannot fetch '%s'", url);
    if (run_git(a, full_argv, 0, OAK_NULL, what, err, err_cap) != 0)
      return -1;
  }

  const char* checkout_argv[] = { git,  "-C",       dest, "checkout",
                                  "--quiet", "--detach", rev,  OAK_NULL };
  char what[OAK_PKG_ERROR_MAX];
  snprintf(what, sizeof what, "cannot check out '%s' from '%s'", rev, url);
  return run_git(a, checkout_argv, 0, OAK_NULL, what, err, err_cap);
}

int oak_pkg_git_fetch(oak_allocator_t* a,
                      const oak_pkg_source_t* source,
                      const char* dest,
                      char out_rev[OAK_PKG_REV_SIZE],
                      char* err,
                      const usize err_cap)
{
  if (!a || !source || !dest || !out_rev)
    return -1;
  out_rev[0] = 0;

  if (source->kind != OAK_PKG_SOURCE_GIT)
    return oak_pkg_fail(err, err_cap, "not a git source");
  if (!source->tag && !source->rev)
    return oak_pkg_fail(err, err_cap,
                        "git source '%s' names neither a tag nor a rev",
                        source->location);

  const int rc =
      source->rev
          ? fetch_rev(a, source->location, source->rev, dest, err, err_cap)
          : clone_tag(a, source->location, source->tag, dest, err, err_cap);
  if (rc != 0)
    return -1;

  /* Ask git what it actually checked out rather than assuming. For a tag this
   * is the whole point: the lock records the commit, so moving the tag later
   * cannot change what an existing lock resolves to. */
  const char* rev_argv[] = { oak_pkg_git_program(), "-C", dest, "rev-parse",
                             "HEAD", OAK_NULL };
  oak_pkg_proc_result_t res;
  memset(&res, 0, sizeof res);
  if (run_git(a, rev_argv, 1, &res, "cannot read the checked-out commit", err,
              err_cap) != 0)
    return -1;

  if (!res.output || !oak_pkg_git_is_rev(res.output))
  {
    oak_pkg_fail(err, err_cap, "git reported '%s' as a commit, which it is not",
                 res.output ? res.output : "");
    oak_pkg_proc_result_free(a, &res);
    return -1;
  }

  /* A rev that was asked for by name must be the one that arrived; anything
   * else means the server answered a different question. */
  if (source->rev && !rev_equal(source->rev, res.output))
  {
    oak_pkg_fail(err, err_cap,
                 "asked '%s' for commit %s but got %s", source->location,
                 source->rev, res.output);
    oak_pkg_proc_result_free(a, &res);
    return -1;
  }

  memcpy(out_rev, res.output, OAK_PKG_REV_SIZE - 1u);
  out_rev[OAK_PKG_REV_SIZE - 1u] = 0;
  oak_pkg_proc_result_free(a, &res);
  return 0;
}
