#pragma once

/*
 * Fixed-width aliases without pulling in system headers.
 * On typical OSes, usize/isize match the width of size_t/ssize_t (same as
 * uintptr/intptr). Include this before <stdint.h> if you use both, to avoid
 * conflicting typedefs.
 */

typedef signed char i8;
typedef unsigned char u8;

#if defined(__SIZEOF_SHORT__) && (__SIZEOF_SHORT__ == 2)
typedef short i16;
typedef unsigned short u16;
#elif defined(__SIZEOF_INT__) && (__SIZEOF_INT__ == 2)
typedef int i16;
typedef unsigned int u16;
#else
typedef short i16;
typedef unsigned short u16;
#endif

#if defined(__SIZEOF_INT__) && (__SIZEOF_INT__ == 4)
typedef int i32;
typedef unsigned int u32;
#elif defined(__SIZEOF_LONG__) && (__SIZEOF_LONG__ == 4)
typedef long i32;
typedef unsigned long u32;
#else
typedef int i32;
typedef unsigned int u32;
#endif

#if defined(__SIZEOF_LONG__) && (__SIZEOF_LONG__ == 8)
typedef long i64;
typedef unsigned long u64;
#elif defined(__SIZEOF_LONG_LONG__) && (__SIZEOF_LONG_LONG__ == 8)
typedef long long i64;
typedef unsigned long long u64;
#elif defined(__SIZEOF_INT__) && (__SIZEOF_INT__ == 8)
typedef int i64;
typedef unsigned int u64;
#else
typedef long long i64;
typedef unsigned long long u64;
#endif

#if defined(_MSC_VER)
#if defined(_WIN64)
typedef unsigned __int64 usize;
typedef __int64 isize;
#else
typedef unsigned int usize;
typedef int isize;
#endif
#else
typedef unsigned long usize;
typedef long isize;
#endif

/*
 * Null pointer constant. C has no real "null type"; this expands to a null
 * pointer of type void* so it converts to any object pointer (and to function
 * pointers on common compilers). Prefer over the NULL macro from <stddef.h>.
 */
#ifndef null
#define null ((void*)0)
#endif

#define OAK_STATIC_ASSERT(name, condition)                                     \
  typedef char oak_static_assert_##name[(condition) ? 1 : -1]

OAK_STATIC_ASSERT(byte_width, sizeof(i8) == 1 && sizeof(u8) == 1);
OAK_STATIC_ASSERT(width_16_bit, sizeof(i16) == 2 && sizeof(u16) == 2);
OAK_STATIC_ASSERT(width_32_bit, sizeof(i32) == 4 && sizeof(u32) == 4);
OAK_STATIC_ASSERT(width_64_bit, sizeof(i64) == 8 && sizeof(u64) == 8);

#undef OAK_STATIC_ASSERT

#if defined(_MSC_VER) && !defined(__STDC_VERSION__)
#undef _Thread_local
#define _Thread_local __declspec(thread)
#endif
