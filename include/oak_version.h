#pragma once

/*
 * Version and platform identity.
 *
 * Two of everything, deliberately.  The macros are what the *consumer* compiled
 * against; the functions are what the acorn library it ends up linked to was
 * built as.  Those differ whenever a program is built against one Oak and run
 * against another -- exactly the case a plugin loader has to detect, so the
 * comparison has to be expressible.
 *
 * OAK_PLATFORM names the binary artifact a compiled package must ship for this
 * host: "<os>-<arch>", e.g. "windows-x86_64", "linux-aarch64", "macos-x86_64".
 * It is resolved at compile time from the toolchain's own predefined macros, so
 * it needs no build define and cannot disagree with the code it is compiled
 * into.
 */

#include "oak_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Keep in sync with `version:` in meson.build.  The version_identity test
 * fails the build if these drift apart. */
#define OAK_VERSION_MAJOR 1
#define OAK_VERSION_MINOR 0
#define OAK_VERSION_PATCH 0

#define OAK_VERSION_STRING "1.0.0"

/* Ordered integer form, for comparisons: 1.2.3 -> 10203. */
#define OAK_VERSION_NUMBER                                                     \
  ((OAK_VERSION_MAJOR)*10000 + (OAK_VERSION_MINOR)*100 + (OAK_VERSION_PATCH))

#define OAK_VERSION_AT_LEAST(major, minor, patch)                              \
  (OAK_VERSION_NUMBER >= ((major)*10000 + (minor)*100 + (patch)))

/* Operating system component of OAK_PLATFORM. */
#if defined(_WIN32)
#define OAK_PLATFORM_OS "windows"
#elif defined(__APPLE__)
#define OAK_PLATFORM_OS "macos"
#elif defined(__linux__)
#define OAK_PLATFORM_OS "linux"
#elif defined(__FreeBSD__)
#define OAK_PLATFORM_OS "freebsd"
#elif defined(__OpenBSD__)
#define OAK_PLATFORM_OS "openbsd"
#elif defined(__NetBSD__)
#define OAK_PLATFORM_OS "netbsd"
#elif defined(__EMSCRIPTEN__)
#define OAK_PLATFORM_OS "emscripten"
#else
#define OAK_PLATFORM_OS "unknown"
#endif

/* Architecture component of OAK_PLATFORM.  Names follow the spelling package
 * authors already use for release artifacts (x86_64 / aarch64), not the
 * toolchain's internal one (amd64 / arm64). */
#if defined(__x86_64__) || defined(_M_X64)
#define OAK_PLATFORM_ARCH "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define OAK_PLATFORM_ARCH "aarch64"
#elif defined(__i386__) || defined(_M_IX86)
#define OAK_PLATFORM_ARCH "x86"
#elif defined(__arm__) || defined(_M_ARM)
#define OAK_PLATFORM_ARCH "arm"
#elif defined(__riscv) && __riscv_xlen == 64
#define OAK_PLATFORM_ARCH "riscv64"
#elif defined(__powerpc64__)
#define OAK_PLATFORM_ARCH "ppc64"
#elif defined(__EMSCRIPTEN__)
#define OAK_PLATFORM_ARCH "wasm32"
#else
#define OAK_PLATFORM_ARCH "unknown"
#endif

#define OAK_PLATFORM OAK_PLATFORM_OS "-" OAK_PLATFORM_ARCH

/* Version of the acorn library this call lands in, e.g. "1.0.0".  Static
 * storage; never freed. */
OAK_API const char* oak_version(void);

/* Platform triple of the acorn library this call lands in, e.g.
 * "linux-x86_64".  Static storage; never freed. */
OAK_API const char* oak_platform(void);

#ifdef __cplusplus
}
#endif
