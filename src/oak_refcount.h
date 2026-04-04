#pragma once

typedef struct
{
  volatile int count;
} oak_refcount_t;

static inline void oak_refcount_init(oak_refcount_t* rc, const int n)
{
  rc->count = n;
}

#if defined(__GNUC__) || defined(__clang__)

static inline void oak_refcount_inc(oak_refcount_t* rc)
{
  __atomic_fetch_add(&rc->count, 1, __ATOMIC_RELAXED);
}

static inline int oak_refcount_dec(oak_refcount_t* rc)
{
  if (__atomic_fetch_sub(&rc->count, 1, __ATOMIC_RELEASE) == 1)
  {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return 1;
  }
  return 0;
}

#elif defined(_MSC_VER)

#include <intrin.h>

#if defined(_M_ARM64)

static inline void oak_refcount_inc(oak_refcount_t* rc)
{
  _InterlockedIncrement_nf((volatile long*)&rc->count);
}

static inline int oak_refcount_dec(oak_refcount_t* rc)
{
  if (_InterlockedDecrement_rel((volatile long*)&rc->count) == 0)
  {
    __dmb(_ARM64_BARRIER_ISH);
    return 1;
  }
  return 0;
}

#else /* _M_IX86 || _M_X64 */

static inline void oak_refcount_inc(oak_refcount_t* rc)
{
  _InterlockedIncrement((volatile long*)&rc->count);
}

static inline int oak_refcount_dec(oak_refcount_t* rc)
{
  return _InterlockedDecrement((volatile long*)&rc->count) == 0;
}

#endif /* _M_ARM64 */

#else
#error "Unsupported compiler: need MSVC, GCC, or Clang"
#endif
