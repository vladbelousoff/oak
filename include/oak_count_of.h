#pragma once

/*
 * OAK_COUNT_OF(arr)
 *
 * Returns the number of elements in a static array.
 * - MSVC: uses _countof (safe, built-in)
 * - GCC/Clang: compile-time error on pointers
 * - Fallback: standard sizeof trick (no pointer protection)
 */

#if defined(_MSC_VER)

#include <stdlib.h>

/* MSVC */
#define OAK_COUNT_OF(arr) _countof(arr)

#elif defined(__GNUC__) || defined(__clang__)

/* GCC / Clang: sizeof(char[-1]) if arr decays to pointer (types match). */
#define OAK_COUNT_OF(arr)                                                      \
  (sizeof(arr) / sizeof((arr)[0]) +                                            \
   (0 * sizeof(char[1 - 2 * __builtin_types_compatible_p(                      \
                                __typeof__(arr), __typeof__(&(arr)[0]))])))

#else

/* Portable fallback */
#define OAK_COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif
