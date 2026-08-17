#pragma once

#include "oak_types.h"

/*
 * oak_utf8_next
 *
 * Reads the next UTF-8 codepoint from the string starting at s.
 *
 * Parameters
 *   s pointer to a UTF-8 encoded string
 *   out pointer where the decoded Unicode codepoint will be written
 *
 * Returns
 *   1..4 number of bytes consumed
 *   0 if s points to the NUL terminator
 *   -1 invalid or malformed UTF-8 sequence
 *
 * Example
 *   const char *p = "héllo";
 *   u32 cp;
 *   int n;
 *
 *   while ((n = oak_utf8_next(p, &cp)) > 0) {
 *       p += n;
 *   }
 */
static int oak_utf8_next(const char* s, u32* out)
{
  const unsigned char* p = (const unsigned char*)s;
  u32 cp;
  int extra;

  if (*p == 0)
  {
    return 0;
  }

  if (*p < 0x80)
  {
    cp = *p;
    extra = 0;
    p++;
  }
  else if (*p < 0xC0)
  {
    return -1;
  }
  else if (*p < 0xE0)
  {
    cp = *p++ & 0x1F;
    extra = 1;
  }
  else if (*p < 0xF0)
  {
    cp = *p++ & 0x0F;
    extra = 2;
  }
  else if (*p < 0xF8)
  {
    cp = *p++ & 0x07;
    extra = 3;
  }
  else
  {
    return -1;
  }

  for (int i = 0; i < extra; i++)
  {
    if ((p[i] & 0xC0) != 0x80)
    {
      return -1;
    }
  }

  for (int i = 0; i < extra; i++)
  {
    cp = (cp << 6) | (p[i] & 0x3F);
  }

  p += extra;

  if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
      (extra == 3 && cp < 0x10000))
  {
    return -1;
  }

  if (cp >= 0xD800 && cp <= 0xDFFF)
  {
    return -1;
  }

  if (cp > 0x10FFFF)
  {
    return -1;
  }

  if (out)
  {
    *out = cp;
  }

  return (int)(p - (const unsigned char*)s);
}

/*
 * oak_utf8_next_bounded
 *
 * Like oak_utf8_next, but never reads past s + avail. Use for buffers that are
 * not NUL-terminated. Returns 0 when avail == 0 or when *s is NUL. Returns -1
 * for invalid UTF-8 or an incomplete sequence at the end of the buffer.
 */
static int oak_utf8_next_bounded(const char* s, usize avail, u32* out)
{
  const unsigned char* p = (const unsigned char*)s;
  u32 cp;
  int extra;

  if (avail == 0)
    return 0;

  if (*p == 0)
  {
    if (out)
      *out = 0;
    return 0;
  }

  if (*p < 0x80)
  {
    cp = *p;
    if (out)
      *out = cp;
    return 1;
  }

  if (*p < 0xC0)
    return -1;
  if (avail < 2)
    return -1;

  if (*p < 0xE0)
  {
    cp = *p++ & 0x1F;
    extra = 1;
  }
  else if (*p < 0xF0)
  {
    if (avail < 3)
      return -1;
    cp = *p++ & 0x0F;
    extra = 2;
  }
  else if (*p < 0xF8)
  {
    if (avail < 4)
      return -1;
    cp = *p++ & 0x07;
    extra = 3;
  }
  else
    return -1;

  for (int i = 0; i < extra; i++)
  {
    if ((p[i] & 0xC0) != 0x80)
      return -1;
  }

  for (int i = 0; i < extra; i++)
    cp = (cp << 6) | (p[i] & 0x3F);

  p += extra;

  if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
      (extra == 3 && cp < 0x10000))
    return -1;

  if (cp >= 0xD800 && cp <= 0xDFFF)
    return -1;

  if (cp > 0x10FFFF)
    return -1;

  if (out)
    *out = cp;

  return (int)(p - (const unsigned char*)s);
}

/*
 * oak_utf8_step
 *
 * Bytes to advance from s to reach the next codepoint boundary, given avail
 * bytes remaining. Always returns at least 1 for a non-empty buffer: a byte
 * that begins no valid sequence -- and an embedded NUL, which Oak strings may
 * carry because they are length-counted -- is stepped over on its own. That
 * keeps every scan below total, so a string of unknown provenance (built by a
 * native binding, say) can never wedge a loop.
 */
static usize oak_utf8_step(const char* s, const usize avail)
{
  if (avail == 0)
    return 0;
  const int n = oak_utf8_next_bounded(s, avail, OAK_NULL);
  return n > 0 ? (usize)n : 1u;
}

/*
 * oak_utf8_count
 *
 * Number of codepoints in the first len bytes of s. This is what a script sees
 * as the length of a string, as opposed to its byte size.
 */
static usize oak_utf8_count(const char* s, const usize len)
{
  usize i = 0;
  usize count = 0;
  while (i < len)
  {
    i += oak_utf8_step(s + i, len - i);
    count++;
  }
  return count;
}

/*
 * oak_utf8_offset
 *
 * Byte offset of codepoint number index, or len when the string holds fewer
 * codepoints than that. The result is always a codepoint boundary, so slicing
 * at it can never cut a character in half.
 */
static usize oak_utf8_offset(const char* s, const usize len, const usize index)
{
  usize i = 0;
  usize count = 0;
  while (i < len && count < index)
  {
    i += oak_utf8_step(s + i, len - i);
    count++;
  }
  return i;
}

/*
 * oak_utf8_index
 *
 * The inverse of oak_utf8_offset: how many codepoints precede byte offset
 * offset. Used to report a byte position found by a byte-wise search (which
 * UTF-8 makes safe) back to a script in codepoint terms.
 */
static usize oak_utf8_index(const char* s, const usize len, usize offset)
{
  usize i = 0;
  usize count = 0;
  if (offset > len)
    offset = len;
  while (i < offset)
  {
    i += oak_utf8_step(s + i, len - i);
    count++;
  }
  return count;
}

/*
 * oak_utf8_is_alpha
 *
 * Returns non-zero if the codepoint is a Unicode letter.
 * Covers ASCII a-z/A-Z plus codepoints >= U+00C0, excluding
 * the multiplication sign (U+00D7) and division sign (U+00F7).
 */
static int oak_utf8_is_alpha(const u32 cp)
{
  if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
    return 1;
  return cp >= 0x00C0 && cp != 0x00D7 && cp != 0x00F7;
}

/*
 * oak_utf8_encode
 *
 * Encodes a Unicode codepoint into UTF-8 bytes.
 *
 * Parameters
 *   cp Unicode codepoint (0..0x10FFFF)
 *   out  pointer to buffer where UTF-8 bytes will be written
 *
 * Returns
 *   number of bytes written (1..4)
 *   0 if cp is invalid
 */
static int oak_utf8_encode(const u32 cp, char* out)
{
  if (cp <= 0x7F)
  {
    // 1-byte ASCII
    out[0] = (char)cp;
    return 1;
  }

  if (cp <= 0x7FF)
  {
    // 2-byte sequence: 110xxxxx 10xxxxxx
    out[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }

  if (cp <= 0xFFFF)
  {
    // Reject surrogate halves
    if (cp >= 0xD800 && cp <= 0xDFFF)
      return 0;
    // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
    out[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }

  if (cp <= 0x10FFFF)
  {
    // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }

  // Invalid codepoint
  return 0;
}
