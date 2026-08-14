#include "oak_stdlib_string.h"

#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_value_impl.h"
#include "oak_vm.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Naive substring search. Returns the byte index of the first occurrence of
 * `needle` within `hay`, or -1 if absent. An empty needle matches at 0. */
static long find_sub(const char* hay,
                     usize hlen,
                     const char* needle,
                     usize nlen)
{
  if (nlen == 0)
    return 0;
  if (nlen > hlen)
    return -1;
  for (usize i = 0; i + nlen <= hlen; ++i)
  {
    if (memcmp(hay + i, needle, nlen) == 0)
      return (long)i;
  }
  return -1;
}

/* Word separators recognised by the case-style conversions. */
static int is_word_sep(char c)
{
  return c == '-' || c == '_' || isspace((unsigned char)c);
}

/* True when the non-separator char at index `i` (i > 0) begins a new word.
 * Besides explicit separators (handled by the caller), word boundaries occur at
 * intra-identifier case transitions: lower/digit -> upper ("fooBar") and the
 * tail of an acronym run -> a capitalized word ("HTTPServer" -> http/server). */
static int starts_word(const char* s, usize len, usize i)
{
  const unsigned char c = (unsigned char)s[i];
  if (!isupper(c))
    return 0;
  const unsigned char prev = (unsigned char)s[i - 1];
  if (islower(prev) || isdigit(prev))
    return 1;
  if (isupper(prev) && i + 1 < len && islower((unsigned char)s[i + 1]))
    return 1;
  return 0;
}

static oak_fn_call_result_t map_case(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc,
                                          oak_value_t* out,
                                          int upper)
{
  const oak_obj_string_t* self;
  if (!oak_arg_string(call, args, argc, 0, &self))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const usize len = self->length;
  if (len == 0)
  {
    *out = oak_vm_string_value_len(call->vm, "", 0);
    return OAK_FN_CALL_OK;
  }
  char* buf = oak_alloc(call->allocator, len, OAK_HERE);
  for (usize i = 0; i < len; ++i)
  {
    const unsigned char c = (unsigned char)self->chars[i];
    buf[i] = (char)(upper ? toupper(c) : tolower(c));
  }
  *out = oak_vm_string_value_len(call->vm, buf, len);
  oak_free(call->allocator, buf, OAK_HERE);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_upper(oak_native_call_t* call,
                                        const oak_value_t* args,
                                        const usize argc,
                                        oak_value_t* out)
{
  return map_case(call, args, argc, out, 1);
}

oak_fn_call_result_t oak_str_lower(oak_native_call_t* call,
                                        const oak_value_t* args,
                                        const usize argc,
                                        oak_value_t* out)
{
  return map_case(call, args, argc, out, 0);
}

oak_fn_call_result_t oak_str_trim(oak_native_call_t* call,
                                       const oak_value_t* args,
                                       const usize argc,
                                       oak_value_t* out)
{
  const oak_obj_string_t* self;
  if (!oak_arg_string(call, args, argc, 0, &self))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const char* s = self->chars;
  usize start = 0;
  usize end = self->length;
  while (start < end && isspace((unsigned char)s[start]))
    ++start;
  while (end > start && isspace((unsigned char)s[end - 1]))
    --end;
  *out = oak_vm_string_value_len(call->vm, s + start, end - start);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_contains(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc,
                                           oak_value_t* out)
{
  const oak_obj_string_t* self;
  const oak_obj_string_t* sub;
  if (!oak_arg_string(call, args, argc, 0, &self) ||
      !oak_arg_string(call, args, argc, 1, &sub))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const long at = find_sub(self->chars, self->length, sub->chars, sub->length);
  *out = OAK_VALUE_BOOL(at >= 0);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_starts_with(oak_native_call_t* call,
                                              const oak_value_t* args,
                                              const usize argc,
                                              oak_value_t* out)
{
  const oak_obj_string_t* self;
  const oak_obj_string_t* pre;
  if (!oak_arg_string(call, args, argc, 0, &self) ||
      !oak_arg_string(call, args, argc, 1, &pre))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int ok = pre->length <= self->length &&
                 memcmp(self->chars, pre->chars, pre->length) == 0;
  *out = OAK_VALUE_BOOL(ok);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_ends_with(oak_native_call_t* call,
                                            const oak_value_t* args,
                                            const usize argc,
                                            oak_value_t* out)
{
  const oak_obj_string_t* self;
  const oak_obj_string_t* suf;
  if (!oak_arg_string(call, args, argc, 0, &self) ||
      !oak_arg_string(call, args, argc, 1, &suf))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int ok =
      suf->length <= self->length &&
      memcmp(self->chars + (self->length - suf->length), suf->chars,
             suf->length) == 0;
  *out = OAK_VALUE_BOOL(ok);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_index_of(oak_native_call_t* call,
                                           const oak_value_t* args,
                                           const usize argc,
                                           oak_value_t* out)
{
  const oak_obj_string_t* self;
  const oak_obj_string_t* sub;
  if (!oak_arg_string(call, args, argc, 0, &self) ||
      !oak_arg_string(call, args, argc, 1, &sub))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const long at = find_sub(self->chars, self->length, sub->chars, sub->length);
  *out = OAK_VALUE_I32((int)at);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_replace(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          const usize argc,
                                          oak_value_t* out)
{
  const oak_obj_string_t* self;
  const oak_obj_string_t* from;
  const oak_obj_string_t* to;
  if (!oak_arg_string(call, args, argc, 0, &self) ||
      !oak_arg_string(call, args, argc, 1, &from) ||
      !oak_arg_string(call, args, argc, 2, &to))
    return OAK_FN_CALL_RUNTIME_ERROR;

  /* An empty needle would match everywhere; return the string unchanged rather
   * than loop forever. */
  if (from->length == 0)
  {
    *out = oak_vm_string_value_len(call->vm, self->chars, self->length);
    return OAK_FN_CALL_OK;
  }

  /* Pass 1: count non-overlapping occurrences. */
  usize count = 0;
  for (usize i = 0; i + from->length <= self->length;)
  {
    if (memcmp(self->chars + i, from->chars, from->length) == 0)
    {
      ++count;
      i += from->length;
    }
    else
      ++i;
  }
  if (count == 0)
  {
    *out = oak_vm_string_value_len(call->vm, self->chars, self->length);
    return OAK_FN_CALL_OK;
  }

  const usize result_len =
      self->length - count * from->length + count * to->length;
  char* buf =
      oak_alloc(call->allocator, result_len == 0 ? 1 : result_len, OAK_HERE);

  /* Pass 2: build the result. */
  usize w = 0;
  for (usize i = 0; i < self->length;)
  {
    if (i + from->length <= self->length &&
        memcmp(self->chars + i, from->chars, from->length) == 0)
    {
      memcpy(buf + w, to->chars, to->length);
      w += to->length;
      i += from->length;
    }
    else
      buf[w++] = self->chars[i++];
  }

  *out = oak_vm_string_value_len(call->vm, buf, result_len);
  oak_free(call->allocator, buf, OAK_HERE);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_repeat(oak_native_call_t* call,
                                         const oak_value_t* args,
                                         const usize argc,
                                         oak_value_t* out)
{
  const oak_obj_string_t* self;
  float count;
  if (!oak_arg_string(call, args, argc, 0, &self) ||
      !oak_arg_number(call, args, argc, 1, &count))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int n = (int)count;
  if (n <= 0 || self->length == 0)
  {
    *out = oak_vm_string_value_len(call->vm, "", 0);
    return OAK_FN_CALL_OK;
  }
  if ((usize)n > (usize)-1 / self->length)
    return oak_native_error(
        call, "repeating %zu bytes %d times overflows", self->length, n);
  const usize total = self->length * (usize)n;
  char* buf = oak_alloc(call->allocator, total, OAK_HERE);
  for (int i = 0; i < n; ++i)
    memcpy(buf + (usize)i * self->length, self->chars, self->length);
  *out = oak_vm_string_value_len(call->vm, buf, total);
  oak_free(call->allocator, buf, OAK_HERE);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_substring(oak_native_call_t* call,
                                            const oak_value_t* args,
                                            const usize argc,
                                            oak_value_t* out)
{
  const oak_obj_string_t* self;
  float from;
  float to;
  if (!oak_arg_string(call, args, argc, 0, &self) ||
      !oak_arg_number(call, args, argc, 1, &from) ||
      !oak_arg_number(call, args, argc, 2, &to))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const long len = (long)self->length;
  long start = (long)from;
  long end = (long)to;
  if (start < 0)
    start = 0;
  if (start > len)
    start = len;
  if (end < start)
    end = start;
  if (end > len)
    end = len;
  *out = oak_vm_string_value_len(call->vm, self->chars + start, (usize)(end - start));
  return OAK_FN_CALL_OK;
}

/* Convert to snake_case: word boundaries (separators and case transitions)
 * become a single underscore and every letter is lowercased.
 * "HelloWorld" / "hello world" / "hello-world" -> "hello_world". */
oak_fn_call_result_t oak_str_to_snake_case(oak_native_call_t* call,
                                               const oak_value_t* args,
                                               const usize argc,
                                               oak_value_t* out)
{
  const oak_obj_string_t* self;
  if (!oak_arg_string(call, args, argc, 0, &self))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const char* s = self->chars;
  const usize len = self->length;
  if (len == 0)
  {
    *out = oak_vm_string_value_len(call->vm, "", 0);
    return OAK_FN_CALL_OK;
  }

  /* At most one underscore per source char, plus the char itself. */
  char* buf = oak_alloc(call->allocator, len * 2, OAK_HERE);
  usize w = 0;
  int pending = 0; /* a separator boundary awaits the next word char */
  for (usize i = 0; i < len; ++i)
  {
    const char c = s[i];
    if (is_word_sep(c))
    {
      if (w > 0)
        pending = 1;
      continue;
    }
    int underscore = 0;
    if (w > 0)
      underscore = pending || starts_word(s, len, i);
    if (underscore)
      buf[w++] = '_';
    pending = 0;
    buf[w++] = (char)tolower((unsigned char)c);
  }

  *out = oak_vm_string_value_len(call->vm, buf, w);
  oak_free(call->allocator, buf, OAK_HERE);
  return OAK_FN_CALL_OK;
}

/* Convert to (lower) camelCase: separators are dropped, the first letter of
 * each following word is uppercased, and the very first letter is lowercased.
 * "hello_world" / "hello world" / "HelloWorld" -> "helloWorld". */
oak_fn_call_result_t oak_str_to_camel_case(oak_native_call_t* call,
                                               const oak_value_t* args,
                                               const usize argc,
                                               oak_value_t* out)
{
  const oak_obj_string_t* self;
  if (!oak_arg_string(call, args, argc, 0, &self))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const char* s = self->chars;
  const usize len = self->length;
  if (len == 0)
  {
    *out = oak_vm_string_value_len(call->vm, "", 0);
    return OAK_FN_CALL_OK;
  }

  /* Separators are removed, so the result never grows past the input. */
  char* buf = oak_alloc(call->allocator, len, OAK_HERE);
  usize w = 0;
  int cap_next = 0; /* uppercase the next word char (after a separator) */
  int started = 0;  /* has the first word char been emitted yet */
  for (usize i = 0; i < len; ++i)
  {
    const char c = s[i];
    if (is_word_sep(c))
    {
      if (started)
        cap_next = 1;
      continue;
    }
    if (!started)
    {
      buf[w++] = (char)tolower((unsigned char)c);
      started = 1;
      cap_next = 0;
    }
    else if (cap_next || starts_word(s, len, i))
    {
      buf[w++] = (char)toupper((unsigned char)c);
      cap_next = 0;
    }
    else
      buf[w++] = (char)tolower((unsigned char)c);
  }

  *out = oak_vm_string_value_len(call->vm, buf, w);
  oak_free(call->allocator, buf, OAK_HERE);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_ord(oak_native_call_t* call,
                                      const oak_value_t* args,
                                      const usize argc,
                                      oak_value_t* out)
{
  const oak_obj_string_t* self;
  if (!oak_arg_string(call, args, argc, 0, &self))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (self->length == 0)
    return oak_native_error(call, "the string is empty");
  *out = OAK_VALUE_I32((int)(unsigned char)self->chars[0]);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t oak_str_chr(oak_native_call_t* call,
                                      const oak_value_t* args,
                                      const usize argc,
                                      oak_value_t* out)
{
  float value;
  if (!oak_arg_number(call, args, argc, 0, &value))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int code = (int)value;
  if (code < 0 || code > 255)
    return oak_native_error(call, "%d is not a byte value (0-255)", code);
  const char c = (char)(unsigned char)code;
  *out = oak_vm_string_value_len(call->vm, &c, 1);
  return OAK_FN_CALL_OK;
}

/* Advance past any trailing whitespace; true iff the string then ends. Used to
 * reject tokens with trailing garbage (e.g. "12x"). */
static int only_trailing_space(const char* endp)
{
  while (*endp && isspace((unsigned char)*endp))
    ++endp;
  return *endp == '\0';
}

/* Parse a string into a number. A token containing '.', 'e', or 'E' is read as
 * a float; otherwise it is read as a base-10 integer. Surrounding whitespace is
 * ignored; anything left over (or an out-of-range integer) is a runtime error. */
oak_fn_call_result_t oak_str_parse_number(oak_native_call_t* call,
                                               const oak_value_t* args,
                                               const usize argc,
                                               oak_value_t* out)
{
  const char* s;
  if (!oak_arg_cstring(call, args, argc, 0, &s))
    return OAK_FN_CALL_RUNTIME_ERROR;

  const char* p = s;
  while (*p && isspace((unsigned char)*p))
    ++p;
  int is_float = 0;
  for (const char* q = p; *q && !isspace((unsigned char)*q); ++q)
  {
    if (*q == '.' || *q == 'e' || *q == 'E')
    {
      is_float = 1;
      break;
    }
  }

  errno = 0;
  char* endp = null;
  if (is_float)
  {
    const float v = strtof(s, &endp);
    if (endp == s || !only_trailing_space(endp))
      return oak_native_error(call, "'%s' is not a number", s);
    *out = OAK_VALUE_F32(v);
    return OAK_FN_CALL_OK;
  }

  const long v = strtol(s, &endp, 10);
  if (endp == s || errno == ERANGE || !only_trailing_space(endp))
    return oak_native_error(call, "'%s' is not a number", s);
  if (v < (long)(-2147483647 - 1) || v > (long)2147483647)
    return oak_native_error(call, "'%s' does not fit in a 32-bit integer", s);
  *out = OAK_VALUE_I32((int)v);
  return OAK_FN_CALL_OK;
}
