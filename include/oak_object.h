#pragma once

#include "oak_export.h"
#include "oak_types.h"

/*
 * Object model: lifetime, runtime type identity, and capability queries.
 *
 * Every object carries a hidden header immediately before the address you
 * hold, recording its vtable and owning allocator. The operations below can
 * therefore recover everything they need from any object pointer, which is
 * why they take `void*` and work for every object the library hands out —
 * containers today, and whatever is added later — with no cast and no
 * wrapper call at the call site.
 *
 * The cost of that convenience is that the compiler cannot check what you
 * pass. Handing one of these a pointer that is not an oak object reads
 * whatever happens to precede it as a vtable and calls through it. Debug
 * builds carry a magic number in the header and assert on it; release builds
 * omit the field entirely and cannot detect the mistake, so exercise new
 * bindings under a debug build. Container operations (oak_container.h) keep
 * their typed `oak_container_t*` parameter and are unaffected.
 *
 * Operation names carry no noun: `oak_destroy` applies to every object, and
 * the same rule extends to the container operations. Descriptive nouns live
 * in the type names instead.
 */

typedef struct oak_container oak_container_t;
typedef struct oak_allocator oak_allocator_t;

/* Runtime type identity. Types form a single parent chain; `oak_is` walks it.
 * A vector's chain is vector -> container -> object. */
typedef struct oak_type_info oak_type_info_t;
struct oak_type_info
{
  const char* name;
  const oak_type_info_t* parent;
};

/* Capabilities an object may support beyond its place in the type chain.
 * Query with `oak_query_interface`; a null result means unsupported. */
typedef enum oak_interface_id oak_interface_id_t;
enum oak_interface_id
{
  OAK_IID_SEQUENCE,      /* positional get/insert/erase/push_back */
  OAK_IID_MAP,           /* keyed find/put/erase_key/contains     */
  OAK_IID_SET,           /* membership add/contains/erase_key     */
  OAK_IID_ITERABLE,      /* begin/next/iter_get                   */
  OAK_IID_RANDOM_ACCESS, /* contiguous storage via oak_data       */
};

/* Releases the object and everything it owns. Null is a no-op. */
OAK_API void oak_destroy(void* obj);

/* The object's most-derived type. Null for a null object. */
OAK_API const oak_type_info_t* oak_type_of(const void* obj);

/* 1 if `obj` is `wanted` or derives from it, walking the parent chain. */
OAK_API int oak_is(const void* obj, const oak_type_info_t* wanted);

/* Returns an interface pointer for `iid`, or null when unsupported. The
 * returned pointer is borrowed and stays valid for the object's lifetime. */
OAK_API void* oak_query_interface(void* obj, oak_interface_id_t iid);

/* The allocator the object was constructed with, recorded in its header. Use
 * it to allocate anything whose lifetime is tied to the object, instead of
 * threading a separate allocator alongside it. */
OAK_API oak_allocator_t* oak_allocator_of(const void* obj);

/* The root of the type chain. `oak_is(obj, oak_object_type_info())` is always
 * true for a live object. */
OAK_API const oak_type_info_t* oak_object_type_info(void);

/* Name of the object's type, or "(null)" when obj is null. For diagnostics. */
OAK_API const char* oak_type_name(const void* obj);
