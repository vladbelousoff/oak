#pragma once

#include "oak_atomic.h"

#ifdef OAK_ATOMIC_REFCOUNT

typedef struct oak_refcount oak_refcount_t;
struct oak_refcount
{
  volatile int count;
};

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

typedef struct oak_refcount oak_refcount_t;
struct oak_refcount
{
  int count;
};

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
