#pragma once

#include "oak_container.h"
#include "oak_export.h"
#include "oak_types.h"

typedef struct oak_allocator oak_allocator_t;

/*
 * Growable contiguous array of fixed-size elements.
 *
 * Implements OAK_IID_SEQUENCE, OAK_IID_RANDOM_ACCESS and OAK_IID_ITERABLE.
 * Keyed operations (oak_put / oak_find / oak_add) are not supported.
 *
 * Storage is one heap block holding the elements; the container header is a
 * separate object, so element pointers never alias container metadata.
 */

/* Creates an empty vector with element size `elem_size` (must be non-zero).
 * Returns null on invalid arguments. Release with
 * `oak_destroy(v)`. */
OAK_API oak_container_t* oak_vector_new(oak_allocator_t* allocator,
                                                  usize elem_size);

/* Element size the vector was created with. */
OAK_API usize oak_vector_elem_size(const oak_container_t* c);

/* Type identity, for `oak_is`. */
OAK_API const oak_type_info_t* oak_vector_type_info(void);
