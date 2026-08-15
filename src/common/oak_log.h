#pragma once

#include "oak_export.h"

#if defined(_MSC_VER)
#define OAK_NORETURN __declspec(noreturn)
#else
#define OAK_NORETURN _Noreturn
#endif

#if defined(_MSC_VER)
#define OAK_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#define OAK_DEBUG_BREAK() __builtin_trap()
#else
#include <signal.h>
#define OAK_DEBUG_BREAK() raise(SIGTRAP)
#endif

#ifdef OAK_DEBUG_LOGGING
#define OAK_ASSERT(condition)                                                  \
  do                                                                           \
  {                                                                            \
    if (!(condition))                                                          \
      OAK_DEBUG_BREAK();                                                       \
  } while (0)
#else
#define OAK_ASSERT(condition)                                                  \
  do                                                                           \
  {                                                                            \
    (void)(condition);                                                         \
  } while (0)
#endif

const char* oak_path_basename(const char* path);
OAK_NORETURN void oak_panic(void);

typedef enum oak_log_level oak_log_level_t;
enum oak_log_level
{
  OAK_LOG_INFO,
  OAK_LOG_DEBUG,
  OAK_LOG_WARN,
  OAK_LOG_ERROR,
};

OAK_API void _oak_log_printf(oak_log_level_t lvl,
#ifdef OAK_DEBUG_LOGGING
                     const char* file,
                     unsigned line,
#endif
                     const char* fmt,
                     ...);

#ifdef OAK_DEBUG_LOGGING

#define OAK_LOG(lvl, fmt, ...)                                                 \
  do                                                                           \
  {                                                                            \
    _oak_log_printf(lvl, __FILE__, __LINE__, fmt, ##__VA_ARGS__);              \
  } while (0)

#define OAK_LOG_COND(cond, lvl, fmt, ...)                                      \
  do                                                                           \
  {                                                                            \
    if (cond)                                                                  \
    {                                                                          \
      _oak_log_printf(lvl, __FILE__, __LINE__, fmt, ##__VA_ARGS__);            \
    }                                                                          \
  } while (0)

#else

#define OAK_LOG(lvl, fmt, ...)                                                 \
  do                                                                           \
  {                                                                            \
    if ((lvl) == OAK_LOG_ERROR || (lvl) == OAK_LOG_INFO)                       \
    {                                                                          \
      _oak_log_printf(lvl, fmt, ##__VA_ARGS__);                                \
    }                                                                          \
  } while (0)

#define OAK_LOG_COND(cond, lvl, fmt, ...)                                      \
  do                                                                           \
  {                                                                            \
    if ((cond) && ((lvl) == OAK_LOG_ERROR || (lvl) == OAK_LOG_INFO))           \
    {                                                                          \
      _oak_log_printf(lvl, fmt, ##__VA_ARGS__);                                \
    }                                                                          \
  } while (0)

#endif
