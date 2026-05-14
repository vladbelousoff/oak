#pragma once

#if defined(_WIN32)
#if defined(OAK_BUILDING_ACORN)
#define OAK_API __declspec(dllexport)
#elif !defined(OAK_STATIC)
#define OAK_API __declspec(dllimport)
#else
#define OAK_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define OAK_API __attribute__((visibility("default")))
#else
#define OAK_API
#endif
