/*
 * Downloading and unpacking an archive dependency.
 *
 * The only file in the project that touches the network, which is deliberate:
 * `acorn`, the `oak` CLI and the wasm build gain no transport dependency at
 * all, because the runtime never fetches -- it reads a lockfile.  That
 * separation is what makes it safe for this one file to link real libraries.
 *
 * Two of them, for two reasons that are not convenience:
 *
 *   libcurl, because certificate verification, redirect policy, proxies and
 *   timeouts are what a hand-rolled downloader gets wrong.  It is configured
 *   to refuse any protocol but HTTPS, including across a redirect, so a
 *   redirect cannot quietly downgrade the connection.
 *
 *   libarchive, because extraction is the dangerous half.  It runs with the
 *   secure flags, so an archive containing "../../.ssh/authorized_keys" or an
 *   absolute path cannot write outside the directory being populated -- a
 *   class of bug that shelling out to `tar` leaves wide open.
 *
 * The order matters as much as the tools: the digest is checked before a
 * single entry is unpacked.  Nothing from an unverified archive ever reaches
 * the filesystem as a file with a name of its choosing.
 */

#include "oak_pkg_tool.h"

#include "internal/oak_pkg_util.h"

#include "oak_pkg_fs.h"
#include "oak_pkg_sha256.h"
#include "oak_version.h"

#include <stdio.h>

#if defined(OAK_PKG_HTTPS)

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>

#if defined(_WIN32)
#include <direct.h>
#define getcwd _getcwd
#define chdir _chdir
#else
#include <unistd.h>
#endif

int oak_pkg_http_available(void)
{
  return 1;
}

static usize write_to_file(char* data, usize size, usize count, void* stream)
{
  return fwrite(data, size, count, (FILE*)stream);
}

/* Fetch `url` into the file at `path`. */
static int download(const char* url,
                    const char* path,
                    char* err,
                    const usize err_cap)
{
  FILE* f = fopen(path, "wb");
  if (!f)
    return oak_pkg_fail(err, err_cap, "cannot write to '%s'", path);

  CURL* c = curl_easy_init();
  if (!c)
  {
    fclose(f);
    return oak_pkg_fail(err, err_cap, "cannot initialize libcurl");
  }

  char errbuf[CURL_ERROR_SIZE];
  errbuf[0] = 0;

  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_to_file);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, (void*)f);
  curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "oak-pkg/" OAK_VERSION_STRING);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
  /* A release URL redirecting to a CDN is completely normal, so follow -- but
   * only ever to another HTTPS URL, and not forever. */
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
#if LIBCURL_VERSION_NUM >= 0x075500 /* 7.85.0 */
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
  curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
  curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
  /* A 404 body is not an archive; treat the status as the answer. */
  curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
  /* No total timeout -- a large package on a slow link is legitimate -- but a
   * connection that has effectively stopped is not. */
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 120L);

  const CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(c);

  const int closed = fclose(f) == 0;

  if (rc != CURLE_OK)
  {
    const char* why = errbuf[0] ? errbuf : curl_easy_strerror(rc);
    if (status >= 400)
      return oak_pkg_fail(err, err_cap, "cannot download '%s': HTTP %ld", url,
                          status);
    return oak_pkg_fail(err, err_cap, "cannot download '%s': %s", url, why);
  }
  if (!closed)
    return oak_pkg_fail(err, err_cap, "cannot finish writing '%s'", path);
  return 0;
}

/* Drop `strip` leading path components. Returns null when the entry is at or
 * above the stripped depth, which is how the wrapper directory itself and any
 * entry outside it are skipped. */
static const char* strip_components(const char* path, int strip)
{
  const char* p = path;
  for (; strip > 0; --strip)
  {
    const char* slash = p;
    while (*slash && *slash != '/' && *slash != '\\')
      ++slash;
    if (!*slash)
      return OAK_NULL;
    p = slash + 1;
    while (*p == '/' || *p == '\\')
      ++p;
  }
  return *p ? p : OAK_NULL;
}

static int copy_entry(struct archive* from, struct archive* to)
{
  for (;;)
  {
    const void* buf = OAK_NULL;
    size_t size = 0;
#if ARCHIVE_VERSION_NUMBER >= 3000000
    la_int64_t offset = 0;
#else
    off_t offset = 0;
#endif
    const int r = archive_read_data_block(from, &buf, &size, &offset);
    if (r == ARCHIVE_EOF)
      return ARCHIVE_OK;
    if (r < ARCHIVE_WARN)
      return r;
    if (archive_write_data_block(to, buf, size, offset) < ARCHIVE_WARN)
      return ARCHIVE_FATAL;
  }
}

/* Copy an entry path into `buf`, refusing anything that is not plainly
 * relative. libarchive's secure flags would catch these too, but a leading
 * separator or a drive letter is worth naming as the hostile input it is
 * rather than reporting as an extraction failure. */
static int safe_relative(const char* path, char* buf, const usize cap)
{
  if (!path || !path[0])
    return -1;
  if (path[0] == '/' || path[0] == '\\')
    return -1;
  if (path[0] && path[1] == ':')
    return -1;
  const usize n = strlen(path);
  if (n + 1u > cap)
    return -1;
  memcpy(buf, path, n + 1u);
  return 0;
}

static int extract(oak_allocator_t* a,
                   const char* archive_path,
                   const char* dest,
                   const int strip,
                   char* err,
                   const usize err_cap)
{
  (void)a;
  struct archive* in = archive_read_new();
  struct archive* out = archive_write_disk_new();
  if (!in || !out)
  {
    if (in)
      archive_read_free(in);
    if (out)
      archive_write_free(out);
    return oak_pkg_fail(err, err_cap, "cannot initialize libarchive");
  }

  /* Format and compression are discovered, not declared, so .tar.gz, .tar.xz,
   * .tar.zst and .zip all work without the manifest saying which. */
  archive_read_support_filter_all(in);
  archive_read_support_format_all(in);

  archive_write_disk_set_options(
      out, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_NODOTDOT |
               ARCHIVE_EXTRACT_SECURE_SYMLINKS |
               ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS);
  archive_write_disk_set_standard_lookup(out);

  /* Extraction runs with the destination as the working directory and entry
   * paths left relative. Prefixing `dest` onto each name instead would make
   * every path absolute, which is exactly what SECURE_NOABSOLUTEPATHS exists
   * to refuse -- the security flags and a rewritten path cannot both be had. */
  char saved_cwd[4096];
  if (!getcwd(saved_cwd, (int)sizeof saved_cwd))
  {
    archive_read_free(in);
    archive_write_free(out);
    return oak_pkg_fail(err, err_cap, "cannot determine the current directory");
  }
  if (chdir(dest) != 0)
  {
    archive_read_free(in);
    archive_write_free(out);
    return oak_pkg_fail(err, err_cap, "cannot enter '%s'", dest);
  }

  int rc = 0;
  if (archive_read_open_filename(in, archive_path, 64u * 1024u) != ARCHIVE_OK)
  {
    rc = oak_pkg_fail(err, err_cap, "cannot read the downloaded archive: %s",
                      archive_error_string(in));
  }
  else
  {
    struct archive_entry* entry;
    int entries = 0;
    for (;;)
    {
      const int r = archive_read_next_header(in, &entry);
      if (r == ARCHIVE_EOF)
        break;
      if (r < ARCHIVE_WARN)
      {
        rc = oak_pkg_fail(err, err_cap, "damaged archive: %s",
                          archive_error_string(in));
        break;
      }

      const char* name = archive_entry_pathname(entry);
      const char* rel = name ? strip_components(name, strip) : OAK_NULL;
      if (!rel)
        continue;

      /* Copied before set_pathname, which frees the storage `rel` points
       * into -- and so that the error message below still has a name. */
      char relbuf[2048];
      if (safe_relative(rel, relbuf, sizeof relbuf) != 0)
      {
        rc = oak_pkg_fail(err, err_cap,
                          "the archive contains an entry that is not a "
                          "relative path, which is not something a package "
                          "does by accident");
        break;
      }
      archive_entry_set_pathname(entry, relbuf);

      /* A hard link names another entry in the same archive, so it needs the
       * same stripping -- and the same refusal to point anywhere else. */
      const char* link = archive_entry_hardlink(entry);
      if (link)
      {
        const char* link_rel = strip_components(link, strip);
        char linkbuf[2048];
        if (!link_rel ||
            safe_relative(link_rel, linkbuf, sizeof linkbuf) != 0)
          continue;
        archive_entry_set_hardlink(entry, linkbuf);
      }

      if (archive_write_header(out, entry) < ARCHIVE_WARN ||
          copy_entry(in, out) < ARCHIVE_WARN ||
          archive_write_finish_entry(out) < ARCHIVE_WARN)
      {
        rc = oak_pkg_fail(err, err_cap, "cannot unpack '%s': %s", relbuf,
                          archive_error_string(out));
        break;
      }
      ++entries;
    }

    /* An archive that produced nothing usually means `strip` is wrong, which
     * is worth saying rather than leaving an empty package to fail later as a
     * missing module. */
    if (rc == 0 && entries == 0)
      rc = oak_pkg_fail(err, err_cap,
                        "the archive had nothing in it after stripping %d "
                        "leading path component%s",
                        strip, strip == 1 ? "" : "s");
  }

  archive_read_close(in);
  archive_read_free(in);
  archive_write_close(out);
  archive_write_free(out);

  /* Restoring this matters even on the failure path: the caller goes on to
   * delete the staging directory and report an error using relative paths. */
  if (chdir(saved_cwd) != 0 && rc == 0)
    rc = oak_pkg_fail(err, err_cap, "cannot return to '%s'", saved_cwd);
  return rc;
}

int oak_pkg_http_fetch(oak_allocator_t* a,
                       const char* url,
                       const char* expect_sha256,
                       const int strip,
                       const char* dest,
                       char out_sha256[OAK_SHA256_HEX_SIZE],
                       char* err,
                       const usize err_cap)
{
  if (!a || !url || !dest || !out_sha256)
    return -1;
  out_sha256[0] = 0;

  if (strncmp(url, "https://", 8u) != 0)
    return oak_pkg_fail(err, err_cap, "'%s' is not an https URL", url);

  const usize dlen = strlen(dest);
  char* archive_path = oak_alloc(a, dlen + 10u, OAK_HERE);
  if (!archive_path)
    return -1;
  snprintf(archive_path, dlen + 10u, "%s.archive", dest);

  int rc = download(url, archive_path, err, err_cap);

  if (rc == 0 && oak_sha256_hex_file(archive_path, out_sha256) != 0)
    rc = oak_pkg_fail(err, err_cap, "cannot hash the downloaded archive");

  /* Before extraction, always. An archive that fails this check has never had
   * a chance to create a file. */
  if (rc == 0 && expect_sha256 &&
      !oak_sha256_hex_equal(expect_sha256, out_sha256))
    rc = oak_pkg_fail(err, err_cap,
                      "'%s' does not match its recorded sha256\n"
                      "  expected %s\n"
                      "  received %s",
                      url, expect_sha256, out_sha256);

  if (rc == 0 && oak_pkg_mkdir_p(dest) != 0)
    rc = oak_pkg_fail(err, err_cap, "cannot create '%s'", dest);

  if (rc == 0)
    rc = extract(a, archive_path, dest, strip, err, err_cap);

  remove(archive_path);
  oak_free(a, archive_path, OAK_HERE);
  return rc;
}

#else /* no libcurl / libarchive */

int oak_pkg_http_available(void)
{
  return 0;
}

int oak_pkg_http_fetch(oak_allocator_t* a,
                       const char* url,
                       const char* expect_sha256,
                       const int strip,
                       const char* dest,
                       char out_sha256[OAK_SHA256_HEX_SIZE],
                       char* err,
                       const usize err_cap)
{
  (void)a;
  (void)expect_sha256;
  (void)strip;
  (void)dest;
  if (out_sha256)
    out_sha256[0] = 0;
  return oak_pkg_fail(err, err_cap,
                      "this oak-pkg cannot download archives, so '%s' is out "
                      "of reach",
                      url);
}

#endif
