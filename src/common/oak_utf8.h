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
    out[0] = (char)(0xC0 | cp >> 6 & 0x1F);
    out[1] = (char)(0x80 | cp & 0x3F);
    return 2;
  }

  if (cp <= 0xFFFF)
  {
    // Reject surrogate halves
    if (cp >= 0xD800 && cp <= 0xDFFF)
      return 0;
    // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
    out[0] = (char)(0xE0 | cp >> 12 & 0x0F);
    out[1] = (char)(0x80 | cp >> 6 & 0x3F);
    out[2] = (char)(0x80 | cp & 0x3F);
    return 3;
  }

  if (cp <= 0x10FFFF)
  {
    // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    out[0] = (char)(0xF0 | cp >> 18 & 0x07);
    out[1] = (char)(0x80 | cp >> 12 & 0x3F);
    out[2] = (char)(0x80 | cp >> 6 & 0x3F);
    out[3] = (char)(0x80 | cp & 0x3F);
    return 4;
  }

  // Invalid codepoint
  return 0;
}
