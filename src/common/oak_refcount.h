#pragma once

#include "oak_atomic.h"

#ifdef OAK_ATOMIC_REFCOUNT

struct oak_refcount_t
{
  volatile int count;
};

static inline void oak_refcount_init(struct oak_refcount_t* rc, const int n)
{
  oak_atomic_int_store_relaxed(&rc->count, n);
}

static inline void oak_refcount_inc(struct oak_refcount_t* rc)
{
  oak_atomic_int_inc_relaxed(&rc->count);
}

static inline int oak_refcount_dec(struct oak_refcount_t* rc)
{
  return oak_atomic_int_dec_release_acquire(&rc->count);
}

static inline int oak_refcount_load(const struct oak_refcount_t* rc)
{
  return oak_atomic_int_load_relaxed(&rc->count);
}

#else /* !OAK_ATOMIC_REFCOUNT */

struct oak_refcount_t
{
  int count;
};

static inline void oak_refcount_init(struct oak_refcount_t* rc, const int n)
{
  rc->count = n;
}

static inline void oak_refcount_inc(struct oak_refcount_t* rc)
{
  rc->count++;
}

static inline int oak_refcount_dec(struct oak_refcount_t* rc)
{
  return --rc->count == 0;
}

static inline int oak_refcount_load(const struct oak_refcount_t* rc)
{
  return rc->count;
}

#endif /* OAK_ATOMIC_REFCOUNT */
