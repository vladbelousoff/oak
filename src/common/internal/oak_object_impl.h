#pragma once

#include "oak_container.h"
#include "oak_log.h"
#include "oak_object.h"
#include "oak_types.h"

#include <stddef.h>

/*
 * Private layouts behind oak_object.h / oak_container.h. Callers never see
 * this file; only the implementations in src/common/oak_*.c include it.
 *
 * Every object is a single allocation laid out as
 *
 *     [ header ][ body ]
 *                ^-- the handle callers hold
 *
 * so the vtable and owning allocator can be recovered from any object pointer
 * by fixed negative offset, without the caller naming a type. That is what
 * lets `oak_destroy` and the other object-level operations accept `void*` and
 * still work for every object the library hands out, present or future.
 *
 * Consequences worth keeping in mind:
 *
 *  - The body is *not* the element storage. A vector's elements live in a
 *    separate allocation, so growing a container never moves its handle.
 *    Handles are stable for the object's lifetime.
 *  - The header is padded to max_align_t, so the body is maximally aligned.
 *    An object body needing stronger alignment than max_align_t is not
 *    supported and would need the pad recorded here to free correctly.
 *  - Passing a non-object pointer to an object-level operation reads whatever
 *    precedes it as a vtable and calls through it. `magic` catches that in
 *    debug builds; nothing catches it in release.
 */

typedef struct oak_base_vtable oak_base_vtable_t;
struct oak_base_vtable
{
  void (*destroy)(void* obj);
  const oak_type_info_t* (*type_of)(const void* obj);
  void* (*query_interface)(void* obj, oak_interface_id_t iid);
};

/* Written into every header and checked by oak_base_check(). Arbitrary, but
 * unlikely to appear by accident in whatever precedes a stray pointer.
 * Debug-only: release builds cannot act on it, so they do not carry it. */
#ifdef OAK_DEBUG_LOGGING
#define OAK_OBJECT_MAGIC 0x0A4B0B1Eu
#endif

/* The `magic` field is present only under OAK_DEBUG_LOGGING, so this struct —
 * and therefore OAK_OBJECT_HEADER_SIZE — differs between debug and release.
 * That is invisible within a build, but a shared library and its consumer
 * must be built with matching settings, as they already must for oak_assert. */
typedef struct oak_base_header oak_base_header_t;
struct oak_base_header
{
  const oak_base_vtable_t* vt;
  oak_allocator_t* allocator;
#ifdef OAK_DEBUG_LOGGING
  u32 magic;
#endif
};

/* Sizing the prefix as a union with the widest fundamental types rounds it up
 * to a multiple of the maximum fundamental alignment, so that
 * base + OAK_OBJECT_HEADER_SIZE is as aligned as the allocation itself.
 * Spelled out rather than using max_align_t, which MSVC only provides in C++. */
typedef union oak_base_prefix oak_base_prefix_t;
union oak_base_prefix
{
  oak_base_header_t header;
  long double align_ld;
  long long align_ll;
  void* align_p;
};

#define OAK_OBJECT_HEADER_SIZE (sizeof(oak_base_prefix_t))

/* The header sitting immediately before `obj`. Takes a const pointer and
 * returns a mutable header because most callers hold a const container and
 * only read through it; the const cast is confined to this one place. */
static inline oak_base_header_t* oak_base_header(const void* obj)
{
  return (oak_base_header_t*)((u8*)obj - OAK_OBJECT_HEADER_SIZE);
}

/* Debug-only guard against a pointer that is not an oak object. Compiles away
 * entirely in release builds, where nothing can catch that mistake. */
static inline void oak_base_check(const void* obj)
{
#ifdef OAK_DEBUG_LOGGING
  oak_assert(oak_base_header(obj)->magic == OAK_OBJECT_MAGIC);
#else
  (void)obj;
#endif
}

/*
 * The container vtable extends the object vtable by embedding it first, so a
 * single vtable pointer in the header serves both levels. Because the public
 * namespace is flat, so is this table: one slot per operation, with a null
 * slot meaning "this implementation does not support that operation". The
 * dispatchers in oak_container.c turn a null slot into a clean 0/null return
 * rather than a crash, and the same information backs the query_interface
 * answers.
 */
typedef struct oak_container_vtable oak_container_vtable_t;
struct oak_container_vtable
{
  oak_base_vtable_t object;

  usize (*size)(const oak_container_t* c);
  void (*clear)(oak_container_t* c);

  /* positional */
  void* (*get)(oak_container_t* c, usize index);
  int (*insert)(oak_container_t* c, usize index, const void* value);
  int (*erase)(oak_container_t* c, usize index);
  int (*push_back)(oak_container_t* c, const void* value);
  int (*pop_back)(oak_container_t* c, void* out_value);
  int (*reserve)(oak_container_t* c, usize capacity);
  int (*resize)(oak_container_t* c, usize count);
  usize (*capacity)(const oak_container_t* c);
  void* (*data)(oak_container_t* c);

  /* keyed */
  void* (*find)(oak_container_t* c, const void* key, usize key_len);
  int (*put)(oak_container_t* c,
             const void* key,
             usize key_len,
             const void* value);
  int (*erase_key)(oak_container_t* c, const void* key, usize key_len);
  int (*contains)(const oak_container_t* c,
                  const void* key,
                  usize key_len);

  /* membership */
  int (*add)(oak_container_t* c, const void* value, usize value_len);

  /* iteration */
  oak_iterator_t (*begin)(oak_container_t* c);
  int (*next)(oak_iterator_t* it);
  void* (*iter_get)(oak_iterator_t* it);
  const void* (*iter_key)(oak_iterator_t* it, usize* out_key_len);
};

/* Type chain roots. Concrete types point their `parent` at the container
 * entry, which in turn points at the object entry. */
extern const oak_type_info_t oak_type_info_object;
extern const oak_type_info_t oak_type_info_container;

/* Allocates [header][body] and installs `vt` and `allocator`. Returns the body
 * pointer — the handle — or null on failure. The body is left uninitialized;
 * the caller fills it in. */
void* oak_base_alloc(oak_allocator_t* allocator,
                       usize body_size,
                       const oak_base_vtable_t* vt);

/* Releases the allocation `obj` belongs to. Does not touch anything the body
 * owns; the implementation's destroy slot frees that first. */
void oak_base_free(void* obj);

/* The allocator recorded in a container's header. */
static inline oak_allocator_t* oak_container_allocator(
    const oak_container_t* c)
{
  return oak_base_header(c)->allocator;
}

/* FNV-1a 32-bit over arbitrary bytes. Shared by the hash map and hash set. */
u32 oak_hash_bytes(const void* data, usize len);

/* Container vtable for `c`, or null when `c` is null. The cast is safe because
 * every container's vtable is a `oak_container_vtable_t` whose first member is
 * the object vtable the header records. */
static inline const oak_container_vtable_t*
oak_container_vt(const oak_container_t* c)
{
  return c ? (const oak_container_vtable_t*)oak_base_header(c)->vt : null;
}
