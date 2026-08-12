#pragma once

#include "oak_object_impl.h"

typedef struct oak_allocator oak_allocator_t;

/*
 * Shared open-addressing core behind oak_hash_map and oak_hash_set.
 *
 * A set is this table with value_size == 0, so both public types are the same
 * storage with different vtables, type identities and query_interface
 * answers. Everything below is usable directly as a container vtable slot.
 *
 * Probing is linear over a power-of-two capacity. Removal writes a tombstone;
 * tombstones count towards the 75% load factor and are reclaimed on rehash.
 * Key pointers are borrowed and compared by content.
 */

/* Allocates a table and installs `vt`. `value_size` may be 0 for a set.
 * Returns null on failure. */
oak_container_t* oak_hash_table_new(
    oak_allocator_t* allocator,
    usize value_size,
    const oak_container_vtable_t* vt);

/* Bytes stored per entry; 0 for a set. */
usize oak_hash_table_value_size(const oak_container_t* c);

/* Vtable slots. */
void oak_hash_table_destroy(void* obj);
usize oak_hash_table_size(const oak_container_t* c);
void oak_hash_table_clear(oak_container_t* c);
void* oak_hash_table_find(oak_container_t* c,
                          const void* key,
                          usize key_len);
int oak_hash_table_put(oak_container_t* c,
                       const void* key,
                       usize key_len,
                       const void* value);
int oak_hash_table_erase_key(oak_container_t* c,
                             const void* key,
                             usize key_len);
int oak_hash_table_contains(const oak_container_t* c,
                            const void* key,
                            usize key_len);
int oak_hash_table_add(oak_container_t* c,
                       const void* value,
                       usize value_len);
oak_iterator_t oak_hash_table_begin(oak_container_t* c);
int oak_hash_table_next(oak_iterator_t* it);
void* oak_hash_table_iter_get(oak_iterator_t* it);
const void* oak_hash_table_iter_key(oak_iterator_t* it,
                                    usize* out_key_len);
