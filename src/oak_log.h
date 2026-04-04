#pragma once

#if defined(_MSC_VER)
#define oak_debug_break() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#define oak_debug_break() __builtin_trap()
#else
#include <signal.h>
#define oak_debug_break() raise(SIGTRAP)
#endif

#ifdef OAK_DEBUG_LOGGING
#define oak_assert(condition)                                                  \
  if (!(condition))                                                            \
  {                                                                            \
    oak_debug_break();                                                         \
  }
#else
#define oak_assert(condition)                                                  \
  do                                                                           \
  {                                                                            \
    (void)(condition);                                                         \
  } while (0)
#endif

const char* oak_filename(const char* path);

typedef enum
{
  OAK_LOG_INF,
  OAK_LOG_DBG,
  OAK_LOG_WRN,
  OAK_LOG_ERR,
} oak_log_level_t;

void _oak_log_printf(oak_log_level_t lvl,
#ifdef OAK_DEBUG_LOGGING
                     const char* file,
                     unsigned line,
#endif
                     const char* fmt,
                     ...);

#ifdef OAK_DEBUG_LOGGING

#define oak_log(lvl, fmt, ...)                                                 \
  do                                                                           \
  {                                                                            \
    _oak_log_printf(lvl, __FILE__, __LINE__, fmt, ##__VA_ARGS__);              \
  } while (0)

#define oak_log_cond(cond, lvl, fmt, ...)                                      \
  do                                                                           \
  {                                                                            \
    if (cond)                                                                  \
    {                                                                          \
      _oak_log_printf(lvl, __FILE__, __LINE__, fmt, ##__VA_ARGS__);            \
    }                                                                          \
  } while (0)

#else

#define oak_log(lvl, fmt, ...)                                                 \
  do                                                                           \
  {                                                                            \
    if ((lvl) == OAK_LOG_ERR || (lvl) == OAK_LOG_INF)                          \
    {                                                                          \
      _oak_log_printf(lvl, fmt, ##__VA_ARGS__);                                \
    }                                                                          \
  } while (0)

#define oak_log_cond(cond, lvl, fmt, ...)                                      \
  do                                                                           \
  {                                                                            \
    if ((cond) && ((lvl) == OAK_LOG_ERR || (lvl) == OAK_LOG_INF))              \
    {                                                                          \
      _oak_log_printf(lvl, fmt, ##__VA_ARGS__);                                \
    }                                                                          \
  } while (0)

#endif
