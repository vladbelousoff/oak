#pragma once

#if defined(_MSC_VER)
#include <intrin.h>
#endif

static inline int oak_atomic_int_inc_relaxed(volatile int* value)
{
#if defined(__GNUC__) || defined(__clang__)
  return __atomic_add_fetch(value, 1, __ATOMIC_RELAXED);
#elif defined(_MSC_VER)
  return (int)_InterlockedIncrement((volatile long*)value);
#else
#error "Unsupported compiler: need MSVC, GCC, or Clang"
#endif
}

static inline int oak_atomic_int_load_relaxed(const volatile int* value)
{
#if defined(__GNUC__) || defined(__clang__)
  return __atomic_load_n(value, __ATOMIC_RELAXED);
#elif defined(_MSC_VER)
  return (int)_InterlockedCompareExchange((volatile long*)value, 0, 0);
#else
#error "Unsupported compiler: need MSVC, GCC, or Clang"
#endif
}

static inline void oak_atomic_int_store_relaxed(volatile int* value,
                                                const int desired)
{
#if defined(__GNUC__) || defined(__clang__)
  __atomic_store_n(value, desired, __ATOMIC_RELAXED);
#elif defined(_MSC_VER)
  _InterlockedExchange((volatile long*)value, desired);
#else
#error "Unsupported compiler: need MSVC, GCC, or Clang"
#endif
}

static inline int oak_atomic_int_dec_release_acquire(volatile int* value)
{
#if defined(__GNUC__) || defined(__clang__)
  if (__atomic_fetch_sub(value, 1, __ATOMIC_RELEASE) == 1)
  {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return 1;
  }
  return 0;
#elif defined(_MSC_VER) && defined(_M_ARM64)
  if (_InterlockedDecrement_rel((volatile long*)value) == 0)
  {
    __dmb(_ARM64_BARRIER_ISH);
    return 1;
  }
  return 0;
#elif defined(_MSC_VER)
  return _InterlockedDecrement((volatile long*)value) == 0;
#else
#error "Unsupported compiler: need MSVC, GCC, or Clang"
#endif
}
