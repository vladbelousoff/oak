#pragma once

#include "oak_types.h"

/* Generic open-addressing hash table.
 *
 * Keys are raw byte sequences (const void*, usize). The FNV-1a hash treats
 * any key as bytes, so strings, integers, compound structs, etc. all work
 * without callbacks or type parameters.
 *
 * Values are int indices (typically into an associated growable array).
 * A return value of -1 from oak_hash_table_get means "not found".
 *
 * Key pointers are borrowed: the caller must ensure the memory they point to
 * remains valid for the lifetime of the table. */

struct oak_hash_table_slot_t
{
  const void* key; /* null = empty slot */
  usize key_len;
  u32 hash; /* cached FNV-1a hash */
  int value;
};

struct oak_hash_table_t
{
  struct oak_hash_table_slot_t* slots; /* heap-allocated       */
  int capacity;                        /* always a power of 2, or 0 */
  int count;
};

void oak_hash_table_init(struct oak_hash_table_t* ht);
void oak_hash_table_free(struct oak_hash_table_t* ht);

/* Inserts key → value. Key pointer must stay valid for the table's lifetime.
 * Behaviour is undefined if the key is already present. */
void oak_hash_table_insert(struct oak_hash_table_t* ht,
                           const void* key,
                           usize key_len,
                           int value);

/* Returns the stored int for the key, or -1 if not found. */
int oak_hash_table_get(const struct oak_hash_table_t* ht,
                       const void* key,
                       usize key_len);
