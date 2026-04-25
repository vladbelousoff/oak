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

#if defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)
#if defined(__SIZEOF_LONG__) && (__SIZEOF_LONG__ == 8)
typedef unsigned long uintptr_t;
typedef long intptr_t;
#elif defined(__SIZEOF_LONG_LONG__) && (__SIZEOF_LONG_LONG__ == 8)
typedef unsigned long long uintptr_t;
typedef long long intptr_t;
#else
typedef unsigned long long uintptr_t;
typedef long long intptr_t;
#endif
#elif defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 4)
#if defined(__SIZEOF_INT__) && (__SIZEOF_INT__ == 4)
typedef unsigned int uintptr_t;
typedef int intptr_t;
#elif defined(__SIZEOF_LONG__) && (__SIZEOF_LONG__ == 4)
typedef unsigned long uintptr_t;
typedef long intptr_t;
#else
typedef unsigned long uintptr_t;
typedef long intptr_t;
#endif
#elif defined(_WIN64)
typedef unsigned long long uintptr_t;
typedef long long intptr_t;
#elif defined(_WIN32)
typedef unsigned long uintptr_t;
typedef long intptr_t;
#else
typedef unsigned long uintptr_t;
typedef long intptr_t;
#endif

typedef uintptr_t usize;
typedef intptr_t isize;

/*
 * Null pointer constant. C has no real "null type"; this expands to a null
 * pointer of type void* so it converts to any object pointer (and to function
 * pointers on common compilers). Prefer over the NULL macro from <stddef.h>.
 */
#ifndef null
#define null ((void*)0)
#endif

_Static_assert(sizeof(i8) == 1 && sizeof(u8) == 1, "oak_types: byte width");
_Static_assert(sizeof(i16) == 2 && sizeof(u16) == 2, "oak_types: 16-bit width");
_Static_assert(sizeof(i32) == 4 && sizeof(u32) == 4, "oak_types: 32-bit width");
_Static_assert(sizeof(i64) == 8 && sizeof(u64) == 8, "oak_types: 64-bit width");
_Static_assert(sizeof(uintptr_t) == sizeof(void*), "oak_types: uintptr width");
