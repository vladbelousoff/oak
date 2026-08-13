#pragma once

/*
 * The native-callback contract: what a C function must look like to be
 * callable from Oak, and what it receives when it is called.
 *
 * This is the header to include when writing native bindings.  It is a facade
 * over oak_value.h rather than a separate set of declarations, because the
 * types are mutually dependent: oak_native_fn_t takes oak_value_t by value, and
 * oak_value.h's native-function object embeds an oak_native_fn_t.  Splitting
 * them would need a three-way cut for no gain — a native callback needs the
 * value accessors (oak_is_string, oak_as_i32, OAK_VALUE_I32, ...) regardless.
 *
 * What lives here conceptually:
 *
 *   oak_native_fn_t        the callback signature
 *   oak_native_call_t      what the callback receives (vm, allocator, user_data)
 *   oak_fn_call_result_t   OAK_FN_CALL_OK / OAK_FN_CALL_RUNTIME_ERROR
 *   oak_attr_runtime_cb_t  the pre-call hook signature for bound attributes
 *
 * Register the callback with oak_bind_fn_global() or oak_bind_fn() from
 * oak_bind.h.
 */

/* No declarations of its own — oak_value.h carries the guard. */
#include "oak_value.h"
