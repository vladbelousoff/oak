#pragma once

/*
 * Reference count operations.  Internal to the library: OAK_ATOMIC_REFCOUNT
 * selects between the atomic and plain implementations, and that choice must
 * never reach a public header — oak_refcount_t's layout is identical either
 * way (see include/oak_refcount.h), so a consumer compiled without the define
 * still matches a library compiled with it.
 */

#include "oak_atomic.h"
#include "oak_refcount.h"

#ifdef OAK_ATOMIC_REFCOUNT

static inline void oak_refcount_init(oak_refcount_t* rc, const int n)
{
  oak_atomic_int_store_relaxed(&rc->count, n);
}

static inline void oak_refcount_inc(oak_refcount_t* rc)
{
  oak_atomic_int_inc_relaxed(&rc->count);
}

static inline int oak_refcount_dec(oak_refcount_t* rc)
{
  return oak_atomic_int_dec_release_acquire(&rc->count);
}

static inline int oak_refcount_load(const oak_refcount_t* rc)
{
  return oak_atomic_int_load_relaxed(&rc->count);
}

#else /* !OAK_ATOMIC_REFCOUNT */

static inline void oak_refcount_init(oak_refcount_t* rc, const int n)
{
  rc->count = n;
}

static inline void oak_refcount_inc(oak_refcount_t* rc)
{
  rc->count++;
}

static inline int oak_refcount_dec(oak_refcount_t* rc)
{
  return --rc->count == 0;
}

static inline int oak_refcount_load(const oak_refcount_t* rc)
{
  return rc->count;
}

#endif /* OAK_ATOMIC_REFCOUNT */
