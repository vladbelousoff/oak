#include "oak_pkg_sha256.h"

#include <stdio.h>
#include <string.h>

/* FIPS 180-4, section 4.2.2: the first 32 bits of the fractional parts of the
 * cube roots of the first 64 primes. */
static const u32 K[64] = {
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
  0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
  0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
  0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
  0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
  0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
  0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
  0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
  0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
  0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
  0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static u32 ror(const u32 x, const unsigned n)
{
  return (x >> n) | (x << (32u - n));
}

static void compress(u32 state[8], const u8 block[64])
{
  u32 w[64];
  for (usize i = 0; i < 16u; ++i)
    w[i] = ((u32)block[i * 4u] << 24) | ((u32)block[i * 4u + 1u] << 16) |
           ((u32)block[i * 4u + 2u] << 8) | (u32)block[i * 4u + 3u];
  for (usize i = 16u; i < 64u; ++i)
  {
    const u32 s0 = ror(w[i - 15u], 7) ^ ror(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
    const u32 s1 = ror(w[i - 2u], 17) ^ ror(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  u32 a = state[0], b = state[1], c = state[2], d = state[3];
  u32 e = state[4], f = state[5], g = state[6], h = state[7];

  for (usize i = 0; i < 64u; ++i)
  {
    const u32 s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
    const u32 ch = (e & f) ^ (~e & g);
    const u32 t1 = h + s1 + ch + K[i] + w[i];
    const u32 s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
    const u32 maj = (a & b) ^ (a & c) ^ (b & c);
    const u32 t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

void oak_sha256_init(oak_sha256_t* h)
{
  if (!h)
    return;
  memset(h, 0, sizeof *h);
  /* Fractional parts of the square roots of the first eight primes. */
  h->state[0] = 0x6a09e667u;
  h->state[1] = 0xbb67ae85u;
  h->state[2] = 0x3c6ef372u;
  h->state[3] = 0xa54ff53au;
  h->state[4] = 0x510e527fu;
  h->state[5] = 0x9b05688cu;
  h->state[6] = 0x1f83d9abu;
  h->state[7] = 0x5be0cd19u;
}

void oak_sha256_update(oak_sha256_t* h, const void* data, usize len)
{
  if (!h || !data)
    return;
  const u8* p = (const u8*)data;
  h->bits += (u64)len * 8u;

  if (h->block_len != 0u)
  {
    const usize want = 64u - h->block_len;
    const usize take = len < want ? len : want;
    memcpy(h->block + h->block_len, p, take);
    h->block_len += take;
    p += take;
    len -= take;
    if (h->block_len < 64u)
      return;
    compress(h->state, h->block);
    h->block_len = 0u;
  }

  while (len >= 64u)
  {
    compress(h->state, p);
    p += 64u;
    len -= 64u;
  }

  if (len != 0u)
  {
    memcpy(h->block, p, len);
    h->block_len = len;
  }
}

void oak_sha256_final(oak_sha256_t* h, u8 out[OAK_SHA256_SIZE])
{
  if (!h || !out)
    return;

  const u64 bits = h->bits;

  /* Padding: a 1 bit, then zeroes, then the length as a 64-bit big-endian
   * count -- which needs a second block when it will not fit in this one. */
  static const u8 pad_start = 0x80u;
  oak_sha256_update(h, &pad_start, 1u);
  h->bits = bits; /* padding is not message length */

  static const u8 zero = 0u;
  while (h->block_len != 56u)
  {
    oak_sha256_update(h, &zero, 1u);
    h->bits = bits;
  }

  u8 tail[8];
  for (usize i = 0; i < 8u; ++i)
    tail[i] = (u8)(bits >> (56u - i * 8u));
  oak_sha256_update(h, tail, 8u);

  for (usize i = 0; i < 8u; ++i)
  {
    out[i * 4u] = (u8)(h->state[i] >> 24);
    out[i * 4u + 1u] = (u8)(h->state[i] >> 16);
    out[i * 4u + 2u] = (u8)(h->state[i] >> 8);
    out[i * 4u + 3u] = (u8)h->state[i];
  }
}

void oak_sha256_hex(const u8 digest[OAK_SHA256_SIZE],
                    char out[OAK_SHA256_HEX_SIZE])
{
  static const char hex[] = "0123456789abcdef";
  for (usize i = 0; i < OAK_SHA256_SIZE; ++i)
  {
    out[i * 2u] = hex[(digest[i] >> 4) & 0xfu];
    out[i * 2u + 1u] = hex[digest[i] & 0xfu];
  }
  out[OAK_SHA256_HEX_SIZE - 1u] = 0;
}

void oak_sha256_hex_buffer(const void* data,
                           const usize len,
                           char out[OAK_SHA256_HEX_SIZE])
{
  oak_sha256_t h;
  u8 digest[OAK_SHA256_SIZE];
  oak_sha256_init(&h);
  oak_sha256_update(&h, data, len);
  oak_sha256_final(&h, digest);
  oak_sha256_hex(digest, out);
}

int oak_sha256_hex_file(const char* path, char out[OAK_SHA256_HEX_SIZE])
{
  FILE* f = fopen(path, "rb");
  if (!f)
    return -1;

  oak_sha256_t h;
  oak_sha256_init(&h);

  u8 buf[64u * 1024u];
  for (;;)
  {
    const usize n = fread(buf, 1u, sizeof buf, f);
    if (n != 0u)
      oak_sha256_update(&h, buf, n);
    if (n != sizeof buf)
      break;
  }
  const int failed = ferror(f) != 0;
  fclose(f);
  if (failed)
    return -1;

  u8 digest[OAK_SHA256_SIZE];
  oak_sha256_final(&h, digest);
  oak_sha256_hex(digest, out);
  return 0;
}

int oak_sha256_hex_equal(const char* a, const char* b)
{
  if (!a || !b)
    return 0;
  usize diff = 0;
  usize i = 0;
  for (; a[i] && b[i]; ++i)
  {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'F')
      ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'F')
      cb = (char)(cb - 'A' + 'a');
    diff |= (usize)(ca ^ cb);
  }
  /* Unequal lengths are unequal digests, but keep the walk length-independent
   * by folding the remaining length in rather than returning early. */
  diff |= (usize)(a[i] != 0) | (usize)(b[i] != 0);
  return diff == 0u;
}
