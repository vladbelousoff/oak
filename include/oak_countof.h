#pragma once

#include <stddef.h>

/*
 * oak_countof(arr)
 *
 * Returns the number of elements in a static array.
 * - MSVC: uses _countof (safe, built-in)
 * - GCC/Clang: compile-time error on pointers
 * - Fallback: standard sizeof trick (no pointer protection)
 */

#if defined(_MSC_VER)

/* MSVC */
#define oak_countof(arr) _countof(arr)

#elif defined(__GNUC__) || defined(__clang__)

/* GCC / Clang: sizeof(char[-1]) if arr decays to pointer (types match). */
#define oak_countof(arr)                                                       \
  (sizeof(arr) / sizeof((arr)[0]) +                                            \
   (0 * sizeof(char[1 - 2 * __builtin_types_compatible_p(                      \
                                __typeof__(arr), __typeof__(&(arr)[0]))])))

#else

/* Portable fallback */
#define oak_countof(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif
