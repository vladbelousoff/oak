#include "oak_pkg_semver.h"

#include <stdio.h>
#include <string.h>

/* Read one dot-separated numeric component. Advances *p past it. Returns -1 on
 * anything that is not a plain run of digits, including a leading zero on a
 * multi-digit number -- "01.0.0" is two different versions depending on who
 * parses it, so it is simply not one. */
static int parse_component(const char** p, u32* out)
{
  const char* s = *p;
  if (*s < '0' || *s > '9')
    return -1;
  if (s[0] == '0' && s[1] >= '0' && s[1] <= '9')
    return -1;

  u32 value = 0;
  while (*s >= '0' && *s <= '9')
  {
    const u32 digit = (u32)(*s - '0');
    /* Cap rather than wrap: a version that overflows is a typo, not a very
     * large release. */
    if (value > (0xFFFFFFFFu - digit) / 10u)
      return -1;
    value = value * 10u + digit;
    ++s;
  }
  *p = s;
  *out = value;
  return 0;
}

static int is_pre_char(const char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '.';
}

int oak_semver_parse(const char* s, oak_semver_t* out)
{
  if (!s || !out)
    return -1;

  oak_semver_t v;
  memset(&v, 0, sizeof v);

  const char* p = s;
  if (parse_component(&p, &v.major) != 0 || *p++ != '.')
    return -1;
  if (parse_component(&p, &v.minor) != 0 || *p++ != '.')
    return -1;
  if (parse_component(&p, &v.patch) != 0)
    return -1;

  if (*p == '-')
  {
    ++p;
    usize n = 0;
    while (is_pre_char(*p))
    {
      if (n + 1u >= sizeof v.pre)
        return -1;
      v.pre[n++] = *p++;
    }
    if (n == 0u)
      return -1;
    v.pre[n] = 0;
  }

  /* Build metadata is not part of a version's identity, so it parses and then
   * disappears -- 1.2.3+a and 1.2.3+b are the same version. */
  if (*p == '+')
  {
    ++p;
    if (!is_pre_char(*p))
      return -1;
    while (is_pre_char(*p))
      ++p;
  }

  if (*p != 0)
    return -1;

  *out = v;
  return 0;
}

int oak_semver_cmp(const oak_semver_t* a, const oak_semver_t* b)
{
  if (a->major != b->major)
    return a->major < b->major ? -1 : 1;
  if (a->minor != b->minor)
    return a->minor < b->minor ? -1 : 1;
  if (a->patch != b->patch)
    return a->patch < b->patch ? -1 : 1;

  /* Having a prerelease makes a version smaller, so "no prerelease" is the
   * largest value here rather than the smallest string. */
  const int a_pre = a->pre[0] != 0;
  const int b_pre = b->pre[0] != 0;
  if (a_pre != b_pre)
    return a_pre ? -1 : 1;
  if (!a_pre)
    return 0;
  const int order = strcmp(a->pre, b->pre);
  return order < 0 ? -1 : (order > 0 ? 1 : 0);
}

int oak_semver_format(const oak_semver_t* v, char* buf, const usize cap)
{
  if (!v || !buf)
    return -1;
  const int n =
      v->pre[0] ? snprintf(buf, cap, "%u.%u.%u-%s", v->major, v->minor,
                           v->patch, v->pre)
                : snprintf(buf, cap, "%u.%u.%u", v->major, v->minor, v->patch);
  if (n < 0 || (usize)n >= cap)
    return -1;
  return n;
}

int oak_semver_req_parse(const char* s, oak_semver_req_t* out)
{
  if (!s || !out)
    return -1;

  while (*s == ' ' || *s == '\t')
    ++s;

  oak_semver_req_t r;
  memset(&r, 0, sizeof r);

  if (s[0] == '*' )
  {
    const char* p = s + 1;
    while (*p == ' ' || *p == '\t')
      ++p;
    if (*p != 0)
      return -1;
    r.op = OAK_SEMVER_ANY;
    *out = r;
    return 0;
  }

  if (s[0] == '^')
  {
    r.op = OAK_SEMVER_CARET;
    ++s;
  }
  else if (s[0] == '>' && s[1] == '=')
  {
    r.op = OAK_SEMVER_ATLEAST;
    s += 2;
  }
  else
  {
    r.op = OAK_SEMVER_EXACT;
  }

  while (*s == ' ' || *s == '\t')
    ++s;

  /* Trailing space is fine, anything else is not, and oak_semver_parse only
   * accepts a whole string -- so trim before handing it over. */
  const char* end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t'))
    --end;

  char trimmed[64];
  const usize len = (usize)(end - s);
  if (len == 0u || len >= sizeof trimmed)
    return -1;
  memcpy(trimmed, s, len);
  trimmed[len] = 0;

  if (oak_semver_parse(trimmed, &r.version) != 0)
    return -1;

  *out = r;
  return 0;
}

int oak_semver_req_match(const oak_semver_req_t* r, const oak_semver_t* v)
{
  if (!r || !v)
    return 0;
  if (r->op == OAK_SEMVER_ANY)
    return 1;

  const int order = oak_semver_cmp(v, &r->version);

  if (r->op == OAK_SEMVER_EXACT)
    return order == 0;

  /* A prerelease is only ever admitted by a constraint written against that
   * exact release, so upgrading a bound never drags in an untagged rc. */
  if (v->pre[0] && !(v->major == r->version.major &&
                     v->minor == r->version.minor &&
                     v->patch == r->version.patch))
    return 0;

  if (order < 0)
    return 0;

  if (r->op == OAK_SEMVER_ATLEAST)
    return 1;

  /* Caret: same leading non-zero component. Below 1.0.0 the minor is what
   * breaks, so ^0.4.1 admits 0.4.x and not 0.5.0. */
  if (r->version.major != 0u)
    return v->major == r->version.major;
  if (r->version.minor != 0u)
    return v->major == 0u && v->minor == r->version.minor;
  return v->major == 0u && v->minor == 0u;
}
