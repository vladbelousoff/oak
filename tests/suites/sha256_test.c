/*
 * SHA-256, against the published vectors.
 *
 * This is the check that decides whether an archive dependency is safe to
 * unpack, so it is worth testing at the boundaries rather than only on "abc":
 * the padding rules change behaviour at exactly 55, 56 and 64 bytes, and a
 * hash that is subtly wrong there would pass a casual test and then reject
 * every real download -- or, far worse, accept the wrong one.
 */

#include "oak_test_support.h"

#include "oak_pkg_sha256.h"

#include <string.h>

OAK_TEST_SUITE(sha256);

static void hash_of(const char* text, char out[OAK_SHA256_HEX_SIZE])
{
  oak_sha256_hex_buffer(text, strlen(text), out);
}

UTEST_F(sha256, hashes_the_empty_input)
{
  char hex[OAK_SHA256_HEX_SIZE];
  hash_of("", hex);
  ASSERT_STREQ(
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", hex);
}

UTEST_F(sha256, hashes_abc)
{
  char hex[OAK_SHA256_HEX_SIZE];
  hash_of("abc", hex);
  ASSERT_STREQ(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
}

/* 56 bytes: the length counter needs a second block, which is the case a
 * hand-written padding loop gets wrong. */
UTEST_F(sha256, hashes_a_two_block_message)
{
  char hex[OAK_SHA256_HEX_SIZE];
  hash_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", hex);
  ASSERT_STREQ(
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", hex);
}

/* 55 bytes: the largest message whose padding still fits in one block. */
UTEST_F(sha256, hashes_a_message_that_exactly_fills_one_block)
{
  char input[56];
  memset(input, 'a', 55u);
  input[55] = 0;

  char hex[OAK_SHA256_HEX_SIZE];
  hash_of(input, hex);
  ASSERT_STREQ(
      "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318", hex);
}

UTEST_F(sha256, hashes_exactly_one_block)
{
  char input[65];
  memset(input, 'a', 64u);
  input[64] = 0;

  char hex[OAK_SHA256_HEX_SIZE];
  hash_of(input, hex);
  ASSERT_STREQ(
      "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", hex);
}

UTEST_F(sha256, hashes_a_million_letters)
{
  oak_sha256_t h;
  oak_sha256_init(&h);

  char chunk[1000];
  memset(chunk, 'a', sizeof chunk);
  for (int i = 0; i < 1000; ++i)
    oak_sha256_update(&h, chunk, sizeof chunk);

  u8 digest[OAK_SHA256_SIZE];
  char hex[OAK_SHA256_HEX_SIZE];
  oak_sha256_final(&h, digest);
  oak_sha256_hex(digest, hex);
  ASSERT_STREQ(
      "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", hex);
}

/* An archive is hashed as it streams off the disk, so feeding it in arbitrary
 * pieces has to be indistinguishable from one call. */
UTEST_F(sha256, streaming_matches_one_shot)
{
  static const char message[] =
      "the quick brown fox jumps over the lazy dog, repeatedly, at length, "
      "until the message spans several compression blocks and then some";

  char whole[OAK_SHA256_HEX_SIZE];
  oak_sha256_hex_buffer(message, sizeof message - 1u, whole);

  static const usize splits[] = { 1u, 3u, 7u, 31u, 64u, 65u, 100u };
  for (usize s = 0; s < sizeof splits / sizeof splits[0]; ++s)
  {
    oak_sha256_t h;
    oak_sha256_init(&h);
    for (usize at = 0; at < sizeof message - 1u; at += splits[s])
    {
      usize n = splits[s];
      if (at + n > sizeof message - 1u)
        n = sizeof message - 1u - at;
      oak_sha256_update(&h, message + at, n);
    }
    u8 digest[OAK_SHA256_SIZE];
    char hex[OAK_SHA256_HEX_SIZE];
    oak_sha256_final(&h, digest);
    oak_sha256_hex(digest, hex);
    ASSERT_STREQ(whole, hex);
  }
}

UTEST_F(sha256, compares_digests_ignoring_case)
{
  ASSERT_TRUE(oak_sha256_hex_equal(
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"));
}

UTEST_F(sha256, rejects_a_digest_that_differs)
{
  ASSERT_FALSE(oak_sha256_hex_equal(
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b856"));
}

/* A truncated digest must not compare equal to the full one it prefixes. */
UTEST_F(sha256, rejects_a_truncated_digest)
{
  ASSERT_FALSE(oak_sha256_hex_equal(
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "e3b0c44298fc1c14"));
  ASSERT_FALSE(oak_sha256_hex_equal("", "abc"));
}
