#pragma once

#include "oak_container.h"
#include "oak_export.h"
#include "oak_types.h"

typedef struct oak_allocator oak_allocator_t;

/*
 * Open-addressing set of borrowed byte-range values.
 *
 * Implements OAK_IID_SET and OAK_IID_ITERABLE. Add with `oak_add`, test with
 * `oak_contains`, remove with `oak_erase_key`. Positional operations are not
 * supported.
 *
 * Shares the hash map's storage strategy: FNV-1a over the value bytes,
 * borrowed value pointers that the caller keeps alive, tombstoned removal.
 */

/* Creates an empty set. Release with `oak_destroy(s)`. */
OAK_API oak_container_t* oak_hash_set_new(
    oak_allocator_t* allocator);

/* Type identity, for `oak_is`. */
OAK_API const oak_type_info_t* oak_hash_set_type_info(void);
