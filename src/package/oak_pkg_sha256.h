#pragma once

/*
 * SHA-256, streaming.
 *
 * This exists because an archive's digest is the only thing standing between a
 * URL and arbitrary code running on a developer's machine, and there is no
 * portable hash to borrow: libcurl's TLS backend differs per platform (Schannel
 * on Windows, OpenSSL elsewhere) and libarchive exports none at all.  It is
 * short, fully specified, and covered by published vectors, so carrying it is
 * cheaper than depending on it.
 *
 * Streaming rather than one-shot so a large archive is hashed as it is read
 * instead of being held in memory whole.
 */

#include "oak_types.h"

#define OAK_SHA256_SIZE 32u
/* 64 hex characters plus the terminator. */
#define OAK_SHA256_HEX_SIZE 65u

typedef struct oak_sha256 oak_sha256_t;
struct oak_sha256
{
  u32 state[8];
  u64 bits;
  u8 block[64];
  usize block_len;
};

void oak_sha256_init(oak_sha256_t* h);
void oak_sha256_update(oak_sha256_t* h, const void* data, usize len);
/* Finalize into `out`.  `h` must not be updated afterwards. */
void oak_sha256_final(oak_sha256_t* h, u8 out[OAK_SHA256_SIZE]);

/* Lowercase hex, NUL-terminated.  Lowercase because that is what every
 * checksum file, release page and `sha256sum` emits, and a digest that only
 * compares equal after case folding invites someone to skip the folding. */
void oak_sha256_hex(const u8 digest[OAK_SHA256_SIZE],
                    char out[OAK_SHA256_HEX_SIZE]);

/* Hash `data` straight to hex. */
void oak_sha256_hex_buffer(const void* data,
                           usize len,
                           char out[OAK_SHA256_HEX_SIZE]);

/* Hash the contents of `path`.  Returns 0, or -1 if it cannot be read. */
int oak_sha256_hex_file(const char* path, char out[OAK_SHA256_HEX_SIZE]);

/* Constant-time comparison of two hex digests, case-insensitively.  Non-zero
 * when they match.  Constant time is not about secrecy here -- the digest is
 * public -- but about never short-circuiting into a partial-match path. */
int oak_sha256_hex_equal(const char* a, const char* b);
