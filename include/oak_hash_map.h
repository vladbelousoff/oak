#pragma once

#include "oak_container.h"
#include "oak_export.h"
#include "oak_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oak_allocator oak_allocator_t;

/*
 * Open-addressing hash map from borrowed byte-range keys to fixed-size values.
 *
 * Implements OAK_IID_MAP and OAK_IID_ITERABLE. Positional operations
 * (oak_get / oak_push_back / oak_data) are not supported: entries have no
 * stable order and storage is not contiguous.
 *
 * Keys are hashed with FNV-1a over their bytes, so strings, integers and
 * packed structs all work without callbacks. Key pointers are borrowed — the
 * caller keeps the pointed-at memory alive for as long as the entry exists.
 * Removal uses tombstones, which are reclaimed on rehash.
 */

/* Creates an empty map storing `value_size` bytes per entry (must be
 * non-zero). Release with `oak_destroy(m)`. */
OAK_API oak_container_t* oak_hash_map_new(
    oak_allocator_t* allocator, usize value_size);

/* Value size the map was created with. */
OAK_API usize oak_hash_map_value_size(const oak_container_t* c);

/* Type identity, for `oak_is`. */
OAK_API const oak_type_info_t* oak_hash_map_type_info(void);

#ifdef __cplusplus
}
#endif
