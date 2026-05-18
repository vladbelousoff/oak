#pragma once

#include "oak_export.h"

struct oak_allocator_t;

/*
 * Generic dynamic-array push / free helpers.
 *
 * Each concrete array type is a plain struct with three fields:
 *   T*  items;
 *   int count;
 *   int capacity;
 *
 * oak_dynarr_init(&arr.items, &arr.count, &arr.capacity)
 *     Zeros all three fields (no allocation). Equivalent to = {0}.
 *
 * oak_dynarr_push(allocator, &arr.items, &arr.count, &arr.capacity,
 *                 &item, sizeof item)
 *     Appends a copy of `item`. Growth: minimum 8 elements, doubles each time.
 *
 * oak_dynarr_free(allocator, &arr.items, &arr.count, &arr.capacity)
 *     Frees the backing array and zeros all three fields.
 *
 * Iteration is plain C:
 *   for (int i = 0; i < arr.count; ++i) { ... arr.items[i] ... }
 */

OAK_API void oak_dynarr_init(void* items_field_ptr, int* count, int* capacity);

OAK_API void oak_dynarr_push(struct oak_allocator_t* a,
                             void* items_field_ptr,
                             int* count,
                             int* capacity,
                             const void* item,
                             int item_size);

OAK_API void oak_dynarr_free(struct oak_allocator_t* a,
                             void* items_field_ptr,
                             int* count,
                             int* capacity);
