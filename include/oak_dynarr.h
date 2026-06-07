#pragma once

#include "oak_export.h"
#include "oak_types.h"

struct oak_allocator_t;

/*
 * Header-backed dynamic arrays exposed as normal typed pointers.
 * Arrays must be initialized before mutation. A null pointer represents an
 * uninitialized or freed array; an initialized empty array owns its header.
 */
OAK_API int oak_dynarr_init(struct oak_allocator_t* allocator,
                            void* array_ref,
                            usize item_size);
OAK_API int oak_dynarr_push(void* array_ref, const void* item);
OAK_API int oak_dynarr_reserve(void* array_ref, int capacity);
OAK_API int oak_dynarr_resize(void* array_ref, int count);
OAK_API int oak_dynarr_pop(void* array_ref, void* out_item);
OAK_API int oak_dynarr_count(const void* items);
OAK_API int oak_dynarr_capacity(const void* items);
OAK_API void oak_dynarr_free(void* array_ref);
